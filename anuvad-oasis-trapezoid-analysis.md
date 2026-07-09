# OASIS Trapezoid 분석 문서

> 대상 소스: `src/oasis/trapezoid.h`, `src/oasis/trapezoid.cc`
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09
> 분석 범위: `Oasis::Trapezoid` 클래스와 압축 사다리꼴(CTrapezoid) 인코딩 지원 로직

---

## 1. 개요

`Oasis::Trapezoid`는 OASIS 파일 포맷 명세(SEMI P35 / OASIS spec)의
**TRAPEZOID** 레코드와 **CTRAPEZOID**(Compressed Trapezoid) 레코드를
메모리상에서 표현하고 조작하기 위한 클래스입니다.

OASIS는 IC 마스크 레이아웃을 저장할 때, 사각형뿐 아니라 한 쌍의 대변이
축(X 또는 Y)에 평행한 사다리꼴(quadrilateral)을 **매우 압축된 형태**로
저장할 수 있습니다. 이 클래스는 다음 두 가지 표현을 동시에 지원합니다:

- **비압축(uncompressed) 사다리꼴**: bounding box 크기(`width`, `height`)와
  두 기울기 파라미터(`delta_a`, `delta_b`)로 정의.
- **압축(compressed) 사다리꼴**: OASIS가 정의한 26가지 표준형(type 0~25) 중
  하나로, 정점 좌표가 `width`/`height`의 단순 함수이므로 파일에 훨씬 작게
  기록됨.

핵심 설계 철학은 **"저장/전송은 압축형으로 하지만, 내부 계산은 항상
동일한 일반 파라미터(orient, width, height, delta_a, delta_b)로 수행"**하는
것입니다. 파일 I/O 계층(parser/writer)은 압축형 번호만 주고받고, 이 클래스가
압축↔비압축 변환을 책임집니다.

---

## 2. 기하학 정의 (OASIS spec 28.6 / 28.7 / Figure 28-1)

사다리꼴은 다음 5개 파라미터로 정의됩니다.

| 파라미터 | 의미 |
|----------|------|
| `orient` | 평행한 두 변의 방향. `Horizontal`(X축 평행) 또는 `Vertical`(Y축 평행) |
| `width` | bounding box의 X축 길이 (≥ 0) |
| `height` | bounding box의 Y축 길이 (≥ 0) |
| `delta_a` | Horizontal: `NW.x - SW.x` / Vertical: `SW.y - SE.y` |
| `delta_b` | Horizontal: `NE.x - SE.x` / Vertical: `NW.y - NE.y` |

`width`/`height`는 signed/unsigned 혼선을 피하기 위해 `long`(부호 있음)으로
저장합니다.

정점 배치 다이어그램 (헤더 주석 발췌):

```
        NW            NE                  + NW
         +------------+                   |\
        /              \                  | \
       /                \                 |  \
      /                  \                |   + NE   orient = Vertical
     /                    \               |   |      delta_a = SW.y - SE.y
    +----------------------+              |   |      delta_b = NW.y - NE.y
   SW                      SE             |   + SE
                                        + SW
       orient = Horizontal
       delta_a = NW.x - SW.x
       delta_b = NE.x - SE.x
```

### 2.1 사각형, 삼각형, 축퇴(degenerate) 형태

- **사각형(Manhattan rectangle)**은 두 방향 모두 만족하므로 `orient`가
  Horizontal이든 Vertical이든 무방합니다. 애플리케이션은 구현이 설정한
  방향에 의존해서는 안 됩니다(헤더 경고).
- **한 변이 길이 0인 삼각형**도 사다리꼴로 간주됩니다(예: type 16~23).
- **축퇴 형태**(면적 0인 선분/점)도 허용되며, 해석은 애플리케이션 의존적임
  (spec 28.9).

### 2.2 불변식 (invariants)

`span = (orient == Horizontal) ? width : height` 일 때:

```
abs(delta_a) <= span
abs(delta_b) <= span
abs(delta_a - delta_b) <= span
```

마지막 식은 **기울기 두 변(slant edges)이 서로 교차하지 않음**을 보장합니다.
이 검증은 `verifyValidity()`가 수행합니다.

---

## 3. 클래스 인터페이스 (`trapezoid.h`)

### 3.1 예외 타입

```cpp
class BadTrapezoidError : public std::exception {
public:
    const char* what() const noexcept override { return "bad trapezoid"; }
};
```

생성자에 무효한 인수(변 교차 등)가 들어오면 던져집니다.

### 3.2 열거형 / 상수

```cpp
enum Orientation { Horizontal, Vertical };
enum { Uncompressed = -1 };   // ctrapType 값으로 사용
```

`ctrapType == -1`이면 비압축 상태, `0..25`이면 압축형 번호입니다.

### 3.3 멤버 변수 (private)

```cpp
Uchar       orient;         // Horizontal or Vertical
short       ctrapType;      // Uncompressed(-1) or 0..25
long        width, height;
long        delta_a, delta_b;
```

`ctrapType`을 `short`로 둔 이유는 `-1`(Uncompressed)과 `0..25`를 모두
표현하기 위해서입니다(`Uchar`로는 음수 불가).

### 3.4 공개 메서드

| 메서드 | 역할 |
|--------|------|
| `Trapezoid(Orientation, w, h, da, db)` | 비압축 생성자. `throw BadTrapezoidError` |
| `Trapezoid(Uint ctrapType, w, h)` | 압축 생성자. `throw BadTrapezoidError, overflow_error` |
| `bool tryCompress()` | 비압축→압축형 번호를 역산해 `ctrapType` 설정. 성공 시 true |
| `bool isCompressed()` | `ctrapType >= 0` |
| `int getCompressType()` | `ctrapType` 반환 |
| `getWidth/getHeight/getDelta_A/getDelta_B` | 파라미터 접근자 |
| `Orientation getOrientation()` | 방향 반환 |
| `void getVertices(Delta pt[4])` | 4 정점(SW, SE, NE, NW 순) 계산 |
| `Delta getVertex(Uint n)` | **DEPRECATED** — 하위 호환용, `getVertices` 권장 |
| `static ctrapezoidTypeIsValid(Ulong)` | `ctrapType <= 25` 검사 |
| `static needWidth(Ulong)` / `static needHeight(Ulong)` | 파일에서 width/height 중 어느 것을 읽어야 하는지 parser가 판단 |

---

## 4. 압축 사다리꼴 인코딩 (`trapezoid.cc`)

### 4.1 설계: 26행 룩업 테이블

OASIS는 26종의 압축 사다리꼴을 정의합니다(type 0~25). 각 type별로
정점 좌표가 `width`/`height`의 단순 선형 함수입니다. 이를 26개 `switch`
케이스로 구현하는 대신, **`CTrapInfo` 테이블 26행**으로 데이터화했습니다.

```cpp
enum CTrapFunc { Z, W, H, W2, H2, mW, mH };
//  Z=0, W=width, H=height, W2=2*width, H2=2*height, mW=-width, mH=-height

struct CTrapInfo {
    Uchar orient;   // Trapezoid::Orientation
    Uchar width;    // CTrapFunc  -> width 계산식
    Uchar height;   // CTrapFunc  -> height 계산식
    Uchar delta_a;  // CTrapFunc
    Uchar delta_b;  // CTrapFunc
};
```

모든 멤버를 `Uchar`로 해 공간을 절약합니다. 행 번호가 spec Figure 8의
ctrapezoid 번호와 1:1 대응합니다.

### 4.2 `ctrapInfo[]` 테이블 전체 (spec Figure 8 매핑)

```
 idx  orient       wd   ht  d_a   d_b   비고
  0   Horizontal   W    H    Z    mH
  1   Horizontal   W    H    Z     H
  2   Horizontal   W    H    H     Z
  3   Horizontal   W    H   mH     Z
  4   Horizontal   W    H    H    mH
  5   Horizontal   W    H   mH     H
  6   Horizontal   W    H    H     H
  7   Horizontal   W    H   mH    mH
  8   Vertical     W    H    Z     W
  9   Vertical     W    H    Z    mW
 10   Vertical     W    H   mW     Z
 11   Vertical     W    H    W     Z
 12   Vertical     W    H   mW     W
 13   Vertical     W    H    W    mW
 14   Vertical     W    H   mW    mW
 15   Vertical     W    H    W     W
 16   Horizontal   W    W    Z    mW     triangle
 17   Horizontal   W    W    Z     W     triangle
 18   Horizontal   W    W    W     Z     triangle
 19   Horizontal   W    W   mW     Z     triangle
 20   Horizontal   H2   H    H    mH     triangle (width=2*height)
 21   Horizontal   H2   H   mH     H     triangle
 22   Vertical     W   W2   mW     W     triangle (height=2*width)
 23   Vertical     W   W2    W    mW     triangle
 24   Horizontal   W    H    Z     Z     rectangle(axial)
 25   Horizontal   W    W    Z     Z     square
```

**핵심 관찰:**
- type 0~15: 일반 사다리꼴(8개 Horizontal + 8개 Vertical 대칭 쌍).
- type 16~23: **삼각형**(한 변 길이 0). `width`/`height` 중 하나만 주어지고
  나머지는 파생됩니다(예: 16~19는 `height`가 `W`로부터 계산, 20~23은
  반대).
- type 24: 축에 평행한 사각형(`delta_a=delta_b=0`).
- type 25: 정사각형(`width==height`, `delta=0`).

### 4.3 `EvalCTrapFunc` — 함수 평가 with 오버플로우 검사

```cpp
long EvalCTrapFunc(Uint func, long width, long height) {
    long val;
    switch (func) {
        case Z:  val = 0;                          break;
        case W:  val = width;                      break;
        case H:  val = height;                     break;
        case W2: val = CheckedPlus(width, width);  break;  // 오버플로우 검사
        case H2: val = CheckedPlus(height, height);break;
        case mH: val = -height;                    break;
        case mW: val = -width;                    break;
    }
    return val;
}
```

`W2`/`H2`(2배) 계산에 `misc/arith.h`의 `CheckedPlus`를 써서
정수 오버플로우를 잡습니다. 압축 생성자는 오버플로우 시 `overflow_error`를
던집니다.

---

## 5. 생성자 로직

### 5.1 비압축 생성자

```cpp
Trapezoid::Trapezoid(Orientation orient, long width, long height,
                     long delta_a, long delta_b)
{
    assert(width >= 0 && height >= 0);
    assert(delta_a != LONG_MIN && delta_b != LONG_MIN); // 부호 반전 오버플로우 방지
    ctrapType = Uncompressed;
    this->orient = orient; this->width = width;
    this->height = height; this->delta_a = delta_a; this->delta_b = delta_b;
    verifyValidity();
}
```

### 5.2 압축 생성자

```cpp
Trapezoid::Trapezoid(Uint ctrapType, long width, long height)
{
    assert(width >= 0 && height >= 0);
    assert(ctrapezoidTypeIsValid(ctrapType));
    const CTrapInfo* info = &ctrapInfo[ctrapType];
    orient   = info->orient;
    this->ctrapType = ctrapType;
    this->width  = EvalCTrapFunc(info->width,  width, height);
    this->height = EvalCTrapFunc(info->height, width, height);
    this->delta_a = EvalCTrapFunc(info->delta_a, this->width, this->height);
    this->delta_b = EvalCTrapFunc(info->delta_b, this->width, this->height);
    verifyValidity();
}
```

테이블의 함수식을 적용해 일반 파라미터로 환원한 뒤 동일하게
`verifyValidity()`로 검증합니다. → **내부 표현은 항상 일반형으로 통일**.

---

## 6. `verifyValidity()` — 유효성 검증

spec 28.9 / 29.8의 체크를 구현합니다.

```cpp
void Trapezoid::verifyValidity() {
    long span = (orient == Horizontal) ? width : height;
    if (abs(CheckedMinus(delta_a, delta_b)) > span
            || abs(delta_a) > span
            || abs(delta_b) > span)
        throw BadTrapezoidError();
}
```

세 불변식을 모두 만족해야 하며, `delta_a - delta_b` 차이도 오버플로우 검사
(`CheckedMinus`)로 계산합니다.

---

## 7. `tryCompress()` — 비압축 → 압축형 역산

이 메서드는 일반 파라미터로 표현된 사다리꼴이 OASIS 26종 중 어느 type으로
압축 가능한지 찾아 `ctrapType`에 기록합니다. **파일 기록 시 크기를
최소화**하는 데 쓰입니다.

알고리즘:

1. 이미 압축 상태(`ctrapType != Uncompressed`)면 즉시 `true`.
2. **Vertical 방향 특수 케이스** (테이블 매칭만으로는 누락되는 type 처리):
   - `width == height` 이고 한 `delta`만 0이 아닌 경우 → type 16~19 중 하나
     (`delta_a>0`→19, `delta_a<0`→17, `delta_b>0`→16, `delta_b<0`→18).
   - `delta_a == delta_b == 0` → type 24 (사각형).
   - 그 외 Vertical은 테이블 루프에서 8~11로 매칭(공간 약간 낭비), 24/25는
     인식 불가하므로 위 특수 코드로 보완.
3. **일반 루프**: 테이블 끝(type 25)에서부터 **역순**으로 탐색.
   - 이유: 테이블 하단 type(16~25)이 상단(0~15)보다 더 compact한
     표현을 가지므로, 가능한 한 가장 작은 type 번호를 선택해 파일 크기 절감.
   - 각 행에 대해 `orient` 일치 + `delta_a/delta_b`가 함수식과 일치 +
     `width/height`가 일치(또는 행의 폭/높이가 `W`/`H` 독립적)하면 해당
     `row`를 `ctrapType`으로 채택.

```cpp
int row = sizeof(ctrapInfo)/sizeof(ctrapInfo[0]);
while (--row >= 0) {
    const CTrapInfo* info = &ctrapInfo[row];
    if (info->orient == orient
            && EvalCTrapFunc(info->delta_a, width, height) == delta_a
            && EvalCTrapFunc(info->delta_b, width, height) == delta_b
            && (info->width == W  || EvalCTrapFunc(info->width, width, height) == width)
            && (info->height == H || EvalCTrapFunc(info->height, width, height) == height)) {
        ctrapType = row;
        return true;
    }
}
return false;
```

매칭 실패 시 `false`를 반환하며, 호출자는 그대로 비압축 TRAPEZOID로 기록합니다.

---

## 8. `getVertices()` — 정점 좌표 계산

bounding box를 항상 `(0,0,width,height)`로 정규화하여 4정점
**SW, SE, NE, NW** 순으로 채웁니다.

### 8.1 Horizontal (X축 평행 대변)

- 하단 두 점 Y=0, 상단 두 점 Y=height.
- `delta_a >= 0`: `SW.x=0`, `NW.x=delta_a` / `delta_a < 0`: `SW.x=-delta_a`, `NW.x=0`
- `delta_b >= 0`: `SE.x=width-delta_b`, `NE.x=width` / `delta_b < 0`: `SE.x=width`, `NE.x=width+delta_b`

### 8.2 Vertical (Y축 평행 대변)

- 좌측 두 점 X=0, 우측 두 점 X=width.
- `delta_a >= 0`: `SW.y=delta_a`, `SE.y=0` / `delta_a < 0`: `SW.y=0`, `SE.y=-delta_a`
- `delta_b >= 0`: `NE.y=height-delta_b`, `NW.y=height` / `delta_b < 0`: `NE.y=height`, `NW.y=height+delta_b`

`Delta`는 `oasis.h`에 정의된 부호 있는 좌표 구조체(`long x, y`)입니다.
일부 정점이 겹칠 수 있음(삼각형/축퇴 케이스).

---

## 9. 파일 I/O 지원 정적 메서드

```cpp
static bool needWidth(Uint ctrapType);   // ctrapInfo[type].width == W 인가
static bool needHeight(Uint ctrapType);  // ctrapInfo[type].height == H 인가
```

OASIS CTRAPEZOID 레코드는 type에 따라 `width`/`height` 중 **하나만**
저장하기도 합니다(다른 하나는 파생). Parser는 이 두 메서드로 어느 필드를
읽어야 하는지 결정합니다. 예: type 16~19는 `height`가 `W`에서 파생되므로
`needHeight()==false`(width만 읽음).

---

## 10. 의존성 및 설계 특징

- **의존성**: `port/compiler.h`(플랫폼 매크로), `oasis.h`(`Delta`, `Uchar`,
  `Uint` 등 기본형), `misc/arith.h`(`CheckedPlus`/`CheckedMinus` 오버플로우
  산술).
- **C++11 스타일**: `override`, `nullptr`(없음), `noexcept`, `static_cast`.
- **성능 우선**: 26-case switch 대신 테이블 룩업 + 역순 탐색으로 분기 최소화
  및 압축 효율 극대화. 모든 멤버 `Uchar`/`short`로 pack.
- **안전성**: `LONG_MIN` 부호 반전 버그 방지 assert, `Checked*` 산술로
  오버플로우를 예외(`overflow_error`)로 전환.
- **하위 호환**: `getVertex(n)`은 DEPRECATED 표시, 신규 코드는 `getVertices`.

---

## 11. 사용 예시 (개념적)

```cpp
// 1) 비압축 사다리꼴 생성
Oasis::Trapezoid t(Oasis::Trapezoid::Horizontal, 100, 50, 10, -10);

// 2) 압축 가능 여부 확인 후 최소 type 획득
if (t.tryCompress())
    cout << "ctrap type = " << t.getCompressType();  // 가능하면 0~25

// 3) 정점 추출 (렌더링/검증용)
Oasis::Delta v[4];
t.getVertices(v);  // v[0]=SW, v[1]=SE, v[2]=NE, v[3]=NW

// 4) 압축형으로 직접 생성 (파일 파싱 시)
Oasis::Trapezoid c(17, 80, 0);  // type 17 triangle
```

---

## 12. 요약

`Oasis::Trapezoid`는 OASIS 레이아웃의 핵심 기하 프리미티브인 사다리꼴을
다룹니다. **내부는 5파라미터 일반 표현으로 통일**하고, **파일 저장/전송은
26종 압축형으로 최소화**하는 이중 구조입니다. 26행 룩업 테이블과 역순
압축 탐색(`tryCompress`)으로 공간 효율을 극대화하며, `verifyValidity`와
오버플로우 검사 산술로 spec 불변식과 정수 안전성을 보장합니다.

---

## 참고: 관련 소스 위치

- `src/oasis/trapezoid.h` — 클래스 선언, 주석 다이어그램, 불변식 설명
- `src/oasis/trapezoid.cc` — 테이블, 생성자, `tryCompress`, `getVertices`
- `src/oasis/oasis.h` — `Delta` 좌표 구조체
- `src/misc/arith.h` — `CheckedPlus` / `CheckedMinus`
- `src/oasis/parser.cc`, `writer.cc` — CTRAPEZOID/TRAPEZOID 레코드 I/O
  (`needWidth`/`needHeight` 사용)
