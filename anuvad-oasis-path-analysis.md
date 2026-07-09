# OASIS Path — 기하학적 의미와 Outline(외곽 폴리곤) 변환 방식

> 대상 소스: `src/oasis/records.h` (PathRecord), `src/oasis/element.h` (PathElem),
> `src/oasis/layout-builder.cc` (beginPath / bbox 확장), `src/oasis/creator.cc` (기록),
> `src/oasis/oasis.h` (PointList)
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09 (개정 — 기하학 의미 + Outline 변환 중점)
> 집중: Path의 기하학적 정의와, 중심선을 외곽 폴리곤(Outline)으로 변환하는 방식

---

## 1. 기하학적 의미

OASIS **PATH** 레코드(spec §24, record id `22`)는 일정한 **반폭(half-width)**
을 가진 **중심선(centerline)** 을 따라 그려지는 선분 도형입니다. 배선,
게이트 등 가는 장방형 도형을 중심선 한 줄로 compact하게 표현합니다.

### 1.1 구성 파라미터와 기하학 역할

| 필드 | 기하학 의미 |
|------|-------------|
| `x, y` | 중심선 **첫 점(P0)의 절대 좌표** (anchor) |
| `ptlist` | P0 이후의 상대 변위 열 → 중심선 점들 `P0, P1, ..., Pn` |
| `halfwidth` | 중심선 양측으로 뻗는 폭의 **절반**. 전체 폭 = `2 × halfwidth` |
| `startExtn` | 시작점(P0) 바깥으로 중심선을 연장하는 길이 |
| `endExtn` | 끝점(Pn) 바깥으로 중심선을 연장하는 길이 |

```
        halfwidth
     <------------>
     ┌────────────┐   ← 상단 외곽선 (centerline + halfwidth·normal)
P0 ──┤  center   ├── P1 ── ... ── Pn
     └────────────┘   ← 하단 외곽선 (centerline - halfwidth·normal)
          ↑
   startExtn (P0 바깥 연장)      endExtn (Pn 바깥 연장)
```

### 1.2 세 가지 기하학적 요소

1. **중심선(centerline)**: `P0 + 누적(Δ)` 로 얻은 점 열. 도형의 뼈대.
2. **반폭(halfwidth)**: 중심선에서 수직(법선) 방향으로 양측 `±halfwidth`
   만큼 떨어진 두 평행선이 외곽을 이룹니다.
3. **Extension**: `startExtn`/`endExtn`이 0이 아니면 중심선 끝점을 그
   길이만큼 법선 방향이 아닌 **중심선 방향으로** 연장한 뒤 외곽을 닫습니다
   (spec §24.3).

### 1.3 불변 / 제약

- `halfwidth >= 0`. `halfwidth == 0` → 0폭 선(렌더링 invisible 또는
  application-dependent).
- 중심선은 **Manhattan / Octangular / AllAngle** 제약을 가질 수 있음
  (`PointList::ListType`, spec Table 7-7).
- GDSII와 달리 OASIS Path는 **끝 처리(end-cap) 형태를 명시적으로 가지지
  않고**, extension 값으로 flush / round / extended 를 흉내냅니다.

---

## 2. Outline(외곽 폴리곤) 변환 방식

Path를 실제로 채우기(fill)하려면 중심선을 **닫힌 외곽 다각형(Outline
polygon)** 으로 변환해야 합니다. 변환은 크게 세 단계입니다.

### 2.1 단계 1 — 중심선 점 열 복원

`PathElem::decodePointList()`(`element.cc`)가 OASIS 바이너리 Blob을
`PointList`(상대 Delta 열)로 풉니다. 절대 중심선 좌표는 P0에 누적:

```
P0 = (x, y)                         // 절대 anchor
Pk = P0 + Σ_{i<k} Δ_i               // k=1..n
```

`PointList`는 6종 ListType(ManhattanHorizFirst=0, ManhattanVertFirst=1,
Manhattan=2, Octangular=3, AllAngle=4, AllAngleDoubleDelta=5)으로 인코딩되며,
각 타입은 기하 제약(수평-수직 교번 / 8방향 / 자유각도)을 가집니다.

### 2.2 단계 2 — 각 선분을 ±halfwidth 로 평행이동 (offset / stroke)

중심선의 연속된 두 점 `A→B` 각각에 대해, **법선(normal) 방향**으로
`±halfwidth` 만큼 평행이동한 네 점을 만듭니다.

```
        B_top = B + hw·n
        A_top = A + hw·n
        A_bot = A - hw·n
        B_bot = B - hw·n

   A_bot ──────────── B_bot      (하단 외곽선)
    │                 │
   A ───────────────── B          (중심선)
    │                 │
   A_top ──────────── B_top      (상단 외곽선)
```

법선 `n` 은 선분 `B-A`를 90° 회전한 단위 벡터입니다:
```
dir = (B.x - A.x, B.y - A.y)
len = |dir|
n   = (-dir.y/len, dir.x/len)      // 왼쪽 법선
```
모든 선분에 대해 상단점(center + hw·n)과 하단점(center - hw·n)을 모아
**외곽 다각형의 양쪽 띠(ribbon)** 를 구성합니다.

### 2.3 단계 3 — 끝 처리 (end treatment) 와 닫힘

중심선 끝점(P0, Pn) 부근的外곽을 닫는 방식은 `startExtn`/`endExtn` 값에
따라 세 가지로 나뉩니다(spec §24.3):

| 케이스 | 기하학 처리 | 외곽 폴리곤 끝 모양 |
|--------|-------------|---------------------|
| `extn == 0` | 중심선 끝에서 수직으로 외곽을 닫음 | **Flush** (사각 끝) |
| `extn == halfwidth` | 끝점을 `halfwidth`만큼 중심선 방향으로 연장 후 닫음 | **Extended** (사각 돌출) |
| `extn > halfwidth` (>0, ≠hw) | 끝점을 `extn`만큼 연장 | Extended (더 긴 돌출) |
| round cap | OASIS는 명시 미지원 — application이 호半圆 근사 | Round (근사) |

`creator.cc`의 `setPathStartExtnScheme` / `setPathEndExtnScheme`은 이 값을
`extnScheme` 바이트의 SS/EE 비트로 인코딩합니다:

```cpp
// creator.cc:2089
setPathStartExtnScheme (&extnScheme, startExtn, halfwidth);
//   startExtn == 0         -> NoExtn (flush)
//   startExtn == halfwidth -> HalfExtn (자동 extended)
//   기타                   -> ExplicitExtn (명시값 기록)
```

### 2.4 전체 Outline 생성 알고리즘 (요약)

```
1. centerline = [P0, P1, ..., Pn]        // decodePointList + 누적
2. 시작점 연장: P0' = P0 - startExtn·(P1-P0 방향)   (startExtn>0 시)
3. 끝점 연장:   Pn' = Pn + endExtn·(Pn-Pn-1 방향)     (endExtn>0 시)
4. 각 선분 A→B 에 대해:
     top_A, top_B = A+hw·n, B+hw·n
     bot_A, bot_B = A-hw·n, B-hw·n
5. Outline 다각형 =
     [ top_P0', top_P1, ..., top_Pn',   ← 상단 외곽 (연장 포함)
       끝 cap (flush/extended),
       bot_Pn', bot_Pn-1, ..., bot_P0', ← 하단 외곽 (역순)
       시작 cap (flush/extended) ]      ← 닫힌 링
```

이 다각형을 렌더러/팬 채우기에 넘기면 Path가 면적으로 그려집니다.

---

## 3. 라이브러리 내 실제 처리 (Anuvad)

Anuvad는 Path를 **중심선 + 반폭으로 보관**하며, 외곽 폴리곤으로의 명시적
변환 함수는 두지 않습니다. 대신 **bounding box 계산 시에만 halfwidth로
박스를 확장**합니다 (`layout-builder.cc`)：

```cpp
/*virtual*/ void
LayoutBuilder::beginPath (Ulong layer, Ulong datatype, long x, long y,
                          long halfwidth, long startExtn, long endExtn,
                          const PointList&  ptlist, const Repetition*  rep)
{
    PathElem*  elem = ly_->pageBuf.construct<PathElem>();
    elem->x = x; elem->y = y;
    elem->halfwidth = halfwidth;
    elem->startExtn = startExtn; elem->endExtn = endExtn;
    Encbuf  enc; encodePointList(enc, ptlist);
    elem->ptlistBlob = ly_->blobStore.insert(enc.ptr(), enc.len());
    Box  box = boxOfPointList(ptlist, x, y);
    {
        Point lo = box.min_corner(); Point hi = box.max_corner();
        bg::expand(box, Point(lo.get<0>() - halfwidth, lo.get<1>() - halfwidth));
        bg::expand(box, Point(hi.get<0>() + halfwidth, hi.get<1>() + halfwidth));
    }
    elem->box = box;
    ...
}
```

**설명:**
- `boxOfPointList(ptlist, x, y)` — 중심선 점 열의 bbox.
- `bg::expand` 두 줄이 **중심선 bbox를 각 변方向으로 `halfwidth`만큼
  팽창**시켜, Path 외곽을 감싸는 bbox를 만듭니다. (extension은 bbox에
  별도 반영 안 됨 — 근사 bounding box.)
- 즉 Anuvad는 **"외곽 폴리곤 생성"은 하지 않고, 공간 탐색용 bbox만
  halfwidth 팽창**합니다. 실제 면 채우기는 다운스트림 툴(GDSII 변환·
  렌더러)이 §2 알고리즘으로 수행합니다.

### 3.1 기록 측 (creator.cc)

`OasisCreator::beginPath`는 spec §27 포맷으로 기록합니다:

```
`22' path-info-byte [layer] [datatype] [half-width]
     [extension-scheme [start-extension] [end-extension]]
     [point-list] [x] [y] [repetition]
path-info-byte ::= EWPXYRDL
```

`halfwidth`, `extnScheme`, `ptlist`, `x/y`를 차례로 씁니다. 중심선과 반폭을
그대로 직렬화하므로 파일 크기가 매우 작습니다(외곽 폴리곤 점들을 일일이
저장하는 GDSII 대비).

---

## 4. 예시

**입력**: `P0=(0,0)`, 중심선 deltas `(10,0),(0,20),(-10,0)`,
`halfwidth=5`, `startExtn=endExtn=0` (flush).

```
중심선:  P0(0,0) → P1(10,0) → P2(10,20) → P3(0,20)

Outline (flush cap):
  상단: (0,5) (10,5) (10,25) (0,25)
  하단: (0,-5) (10,-5) (10,15) (0,15)
  닫힌 링:
   (0,5)→(10,5)→(10,25)→(0,25)→[끝 cap]→(0,15)→(10,15)→(10,-5)→(0,-5)→[시작 cap]→(0,5)

bbox (Anuvad): x∈[-5,15], y∈[-5,25]   (중심선 bbox [0,10]×[0,20] 에 halfwidth 팽창)
```

---

## 5. 요약

- **Path의 기하학**: 중심선 점 열 + `halfwidth`(반폭) + `start/endExtn`
  (끝 연장). OASIS는 중심선 한 줄로 배선 도형을 compact 표현.
- **Outline 변환**: 중심선 각 선분을 법선 방향 `±halfwidth`로 평행이동해
  외곽 띠를 만들고, 끝점은 `extn` 값에 따라 flush / extended / round 근사로
  닫아 **닫힌 외곽 다각형**을 생성(§2 알고리즘).
- **Anuvad 실제 처리**: 외곽 폴리곤 생성은 하지 않음. `layout-builder.cc`가
  bbox만 `±halfwidth`로 팽창(`bg::expand`)해 공간 탐색용 박스를 만듦.
  기록은 `creator.cc`가 중심선+반폭을 그대로 직렬화(§27 포맷).

---

## 참고: 관련 소스 위치

- `src/oasis/records.h` — `PathRecord` 정의
- `src/oasis/element.h` — `PathElem` (`decodePointList` 선언, `box` 캐시)
- `src/oasis/element.cc` — `PathElem::decodePointList()` (중심선 점 열 복원)
- `src/oasis/layout-builder.cc` — `beginPath`, **bbox에 halfwidth 팽창** (§3 인용)
- `src/oasis/creator.cc` — `beginPath` 기록(spec §27), `extnScheme` 인코딩
- `src/oasis/oasis.h` — `PointList`, `ListType`, `Delta`
