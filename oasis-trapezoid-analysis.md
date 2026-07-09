# OASIS Trapezoid — 기하학 정의와 정점 좌표 계산

> 대상 소스: `src/oasis/trapezoid.h`, `src/oasis/trapezoid.cc`
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09 (개정)
> 집중: 기하학 정의 + `getVertices()` 정점 계산 로직 (코드 포함 상세 설명)

---

## 1. 기하학 정의 (OASIS spec 28.6 / 28.7 / Figure 28-1)

사다리꼴(Trapezoid)은 **한 쌍의 대변이 축(X 또는 Y)에 평행한 사각형형
편(quadrilateral)**입니다. OASIS는 이를 bounding box 크기와 두 기울기
파라미터로 정의합니다.

### 1.1 파라미터 5개

| 파라미터 | 의미 |
|----------|------|
| `orient` | 평행한 두 변의 방향. `Horizontal`(X축 평행) 또는 `Vertical`(Y축 평행) |
| `width`  | bounding box의 X축 길이 (≥ 0) |
| `height` | bounding box의 Y축 길이 (≥ 0) |
| `delta_a` | Horizontal: `NW.x - SW.x` / Vertical: `SW.y - SE.y` |
| `delta_b` | Horizontal: `NE.x - SE.x` / Vertical: `NW.y - NE.y` |

- `width`/`height`는 signed/unsigned 혼선을 피해 `long`(부호 있음)으로 저장.
- 정점 이름: **SW**(southwest, 좌하), **SE**(southeast, 우하),
  **NE**(northeast, 우상), **NW**(northwest, 좌상).

### 1.2 방향별 기하학 다이어그램

**Horizontal** (두 변 X축 평행 — 위/아래 변이 수평):

```
        NW            NE
         +------------+
        /              \
       /                \
      /                  \
     /                    \
    +----------------------+
   SW                      SE

   delta_a = NW.x - SW.x
   delta_b = NE.x - SE.x
```

**Vertical** (두 변 Y축 평행 — 좌/우 변이 수직):

```
           + NW
           |\
           | \
           |  \
           |   + NE
           |   |
           |   |
           |   + SE
           |  /
           | /
           |/
           + SW

   delta_a = SW.y - SE.y
   delta_b = NW.y - NE.y
```

### 1.3 면적 0이 아닌 일반 사다리꼴의 형태

기울기 파라미터 부호 조합에 따라 4가지 기본 형태가 나옵니다
(`trapezoid.cc` 주석의 ASCII 다이어그램 발췌). `w, da, db`는 각각
`width, delta_a, delta_b`입니다.

**Horizontal — 4가지 부호 케이스:**

```
 delta_a >= 0    delta_a >= 0     delta_a < 0     delta_a < 0
 delta_b >= 0    delta_b < 0      delta_b >= 0    delta_b < 0

   da      w       da   w+db      0        w      0       w+db
   +-------+       +------+       +--------+      +--------+
  /       /       /        \       \      /        \        \
 /       /       /          \       \    /          \        \
+-------+       +------------+       +----+          +--------+
 0    w-db      0            w      -da  w-db       -da       w
```

**Vertical — 4가지 부호 케이스:**

```
 delta_a >= 0   delta_a >= 0    delta_a < 0    delta_a < 0
 delta_b >= 0   delta_b < 0     delta_b >= 0   delta_b < 0

  h +                + h         h +                + h
    |\              /|             |\              /|
    | \            / |             | \            / |
    |  \     h+db +  |             |  + h-db     /  |
 da +   + h-db     |  |             |  |     h+db +  + -da
     \  |        da +  |             |  + -da      |  /
      \ |            \ |             | /           | /
       \|             \|             |/            |/
        + 0             + 0        0 +           0 +
```

### 1.4 특수 형태 (축퇴 / 삼각형 / 사각형)

- **사각형(Manhattan rectangle)**: `delta_a == delta_b == 0`. 두 방향 모두
  만족 → `orient`는 Horizontal/VERTICAL 어느 쪽이든 무방(구현 의존 금지).
- **삼각형**: 한 변이 길이 0. OASIS 압축형 type 16~23이 이에 해당.
- **축퇴(degenerate)**: 면적 0인 선분 또는 점. 해석은 애플리케이션 의존
  (spec 28.9).

### 1.5 불변식 (validity invariants)

`span = (orient == Horizontal) ? width : height` 일 때:

```
abs(delta_a) <= span
abs(delta_b) <= span
abs(delta_a - delta_b) <= span
```

마지막 식은 **기울기 두 변(slant edges)이 서로 교차하지 않음**을 보장합니다.
위반 시 `BadTrapezoidError` 예외가 던져집니다(`verifyValidity()`).

---

## 2. 정점 좌표 계산 — `getVertices()`

`getVertices(Delta pt[4])`는 사다리꼴의 4개 정점을
**SW, SE, NE, NW** 순서로 채웁니다. 좌표는 항상 bounding box가
`(0, 0, width, height)`가 되도록 정규화됩니다(즉, 사다리꼴을 원점에
맞춘 로컬 좌표). 일부 정점이 겹칠 수 있습니다(삼각형/축퇴).

`Delta`는 `oasis.h`에 정의된 부호 있는 2D 좌표 구조체(`long x, y`)입니다.

### 2.1 전체 소스 코드

아래는 `trapezoid.cc`의 `getVertices()` 실제 구현입니다.

```cpp
void
Trapezoid::getVertices (/*out*/ Delta pt[]) const
{
    if (orient == Horizontal) {
        pt[0].y = pt[1].y = 0;
        pt[2].y = pt[3].y = height;

        if (delta_a >= 0) {
            pt[0].x = 0;
            pt[3].x = delta_a;
        } else {
            pt[0].x = -delta_a;
            pt[3].x = 0;
        }
        if (delta_b >= 0) {
            pt[1].x = width - delta_b;
            pt[2].x = width;
        } else {
            pt[1].x = width;
            pt[2].x = width + delta_b;
        }
    }
    else {                              // orient == Vertical
        pt[0].x = pt[3].x = 0;
        pt[1].x = pt[2].x = width;

        if (delta_a >= 0) {
            pt[0].y = delta_a;
            pt[1].y = 0;
        } else {
            pt[0].y = 0;
            pt[1].y = -delta_a;
        }
        if (delta_b >= 0) {
            pt[2].y = height - delta_b;
            pt[3].y = height;
        } else {
            pt[2].y = height;
            pt[3].y = height + delta_b;
        }
    }
}
```

### 2.2 Horizontal 방향 상세 설명

Horizontal은 **위/아래 변이 X축에 평행**합니다.

**Y 좌표 (항상 고정):**
- 하단 두 점 `SW(pt[0])`, `SE(pt[1])` → `y = 0`
- 상단 두 점 `NE(pt[2])`, `NW(pt[3])` → `y = height`

**X 좌표 — `delta_a`(좌측 기울기, SW↔NW 간격) 처리:**

| 조건 | `SW.x (pt[0])` | `NW.x (pt[3])` | 기하학 의미 |
|------|----------------|----------------|-------------|
| `delta_a >= 0` | `0` | `delta_a` | NW가 SW보다 오른쪽으로 `delta_a`만큼 시프트 |
| `delta_a < 0`  | `-delta_a` | `0` | NW가 SW보다 왼쪽 → bounding box를 오른쪽으로 `-delta_a`만큼 밀어 정규화 |

`delta_a < 0` 케이스는 좌측 변이 안쪽으로 기울 때, 전체 도형을 X축 양의
방향으로 `-delta_a`만큼 이동시켜 bounding box 좌측을 `x=0`에 맞춥니다.

**X 좌표 — `delta_b`(우측 기울기, SE↔NE 간격) 처리:**

| 조건 | `SE.x (pt[1])` | `NE.x (pt[2])` | 기하학 의미 |
|------|----------------|----------------|-------------|
| `delta_b >= 0` | `width - delta_b` | `width` | NE가 SE보다 왼쪽으로 `delta_b`만큼 |
| `delta_b < 0`  | `width` | `width + delta_b` | NE가 SE보다 오른쪽으로 `|delta_b|`만큼 → 우측 경계를 `width+delta_b`로 확장 |

**예시 (Horizontal, width=100, height=50, delta_a=10, delta_b=-10):**

```
delta_a >= 0, delta_b < 0 케이스
  SW = (0, 0)
  SE = (100, 0)
  NE = (100+ (-10), 50) = (90, 50)
  NW = (10, 50)
```

```
    NW(10,50)------+ (bounding box 우측은 100이지만 NE가 90)
    /              |
   /               |
  +---------------+
 SW(0,0)         SE(100,0)
```

### 2.3 Vertical 방향 상세 설명

Vertical은 **좌/우 변이 Y축에 평행**합니다.

**X 좌표 (항상 고정):**
- 좌측 두 점 `SW(pt[0])`, `NW(pt[3])` → `x = 0`
- 우측 두 점 `SE(pt[1])`, `NE(pt[2])` → `x = width`

**Y 좌표 — `delta_a`(하단 기울기, SW↔SE 간격) 처리:**

| 조건 | `SW.y (pt[0])` | `SE.y (pt[1])` | 기하학 의미 |
|------|----------------|----------------|-------------|
| `delta_a >= 0` | `delta_a` | `0` | SE가 SW보다 아래로 `delta_a`만큼 |
| `delta_a < 0`  | `0` | `-delta_a` | SE가 SW보다 위로 → bounding box를 아래로 `-delta_a`만큼 정규화 |

**Y 좌표 — `delta_b`(상단 기울기, NW↔NE 간격) 처리:**

| 조건 | `NE.y (pt[2])` | `NW.y (pt[3])` | 기하학 의미 |
|------|----------------|----------------|-------------|
| `delta_b >= 0` | `height - delta_b` | `height` | NW가 NE보다 아래로 `delta_b`만큼 |
| `delta_b < 0`  | `height` | `height + delta_b` | NW가 NE보다 위로 → 상단 경계를 `height+delta_b`로 확장 |

**예시 (Vertical, width=100, height=50, delta_a=10, delta_b=-10):**

```
delta_a >= 0, delta_b < 0 케이스
  SW = (0, 10)
  SE = (100, 0)
  NE = (100, 50)
  NW = (0, 50 + (-10)) = (0, 40)
```

```
  NW(0,40) +----+ NE(100,50)
          /      \
         /        \
  SW(0,10)+--------+ SE(100,0)
```

### 2.4 정점 순서와 방향성

반환 순서는 **SW(0), SE(1), NE(2), NW(3)** — 즉, 좌하에서 시작해
시계반대방향(counter-clockwise, 양의 방향)으로 순회합니다. 이 순서는
다각형 와인딩 규칙, 면적 계산, 렌더링 팬 채우기에 직접 쓰입니다.

### 2.5 하위 호환 `getVertex(n)`

```cpp
inline Delta
Trapezoid::getVertex (Uint n) const
{
    assert (n < 4);
    Delta  vertices[4];
    getVertices(vertices);
    return vertices[n];
}
```

개별 정점을 하나씩 얻는 레거시 API. 내부적으로 `getVertices`를 호출하므로
동일 결과를 반환합니다. **DEPRECATED** — 신규 코드는 `getVertices`를
사용하세요.

---

## 3. 요약

- 사다리꼴은 `orient` + `width/height` + `delta_a/delta_b` 5파라미터로
  정의되며, 방향에 따라 기하학 의미가 다릅니다.
- `getVertices()`는 bounding box를 `(0,0,width,height)`로 정규화한 로컬
  좌표의 4정점(SW, SE, NE, NW)을 계산합니다.
- 부호(`delta >= 0` / `< 0`)에 따라 정점이 bounding box 내부로 시프트하거나
  경계를 확장하며, 음수 delta는 도형을 원점에 맞추기 위해 보정됩니다.
- 이 계산은 압축/비압축 여부와 무관하게 **항상 동일한 일반 파라미터**에서
  수행되므로, 파일 I/O 계층은 압축형 번호만 다루고 기하 계산은 이 메서드에
  집중됩니다.

---

## 참고: 관련 소스 위치

- `src/oasis/trapezoid.h` — 파라미터 정의, 불변식 주석, 다이어그램
- `src/oasis/trapezoid.cc` — `getVertices()` 구현 (위 인용), `verifyValidity()`
- `src/oasis/oasis.h` — `Delta` 좌표 구조체 정의
