# PointGrouper — GDSII→OASIS Position-to-Repetition Optimizer

> Anuvad 0.10 — `conv/ptgroup.h` + `conv/ptgroup.cc` (896 lines)
> SoftJin Technologies

---

## 1. 역할 (What & Why)

`GdsToOasisConverter`에서 **ElementManager가 동일 element를 병합**한 결과물:

```
SrefInfo (cell="INV", mag=1.0, angle=0):
  positions = [(0,0), (100,0), (200,0), (0,100), (100,100), (200,100), (50,50)]
```

- 이 `positions`는 **같은 element가 등장하는 모든 위치의 집합**
- GDSII라면 이걸 그냥 7개 SREF record로 써야 함 (7×overhead)
- OASIS는 `positions`를 **12종 Repetition** 중 가장 압축률 높은 형태로 변환 가능

**PointGrouper의 임무:** positions 집합에서 규칙적인 패턴(Matrix, UniformX/Y 등)을 찾아 최적의 Repetition으로 분할한다.

---

## 2. 최적화 레벨 (optLevel)

| Level | 행동 | 결과 Repetition 타입 |
|---|---|---|
| **0** | 아무 패턴 탐색 안 함. 모든 점을 한꺼번에 Arbitrary로 묶음 | `Rep_Arbitrary` |
| **1** | 점 정렬 + GCD grid 계산 | `Rep_GridArbitrary` (grid 기반) |
| **> 1** | Sparse matrix 구축 → Matrix/Line 패턴 탐색 → 남은 점 varying | Matrix, UniformX/Y, VaryingX/Y, GridArbitrary |

`gds2oasis.cc`의 `-O opt` 옵션으로 제어 (기본값: 1).

---

## 3. 데이터 구조: Sparse Matrix

optLevel > 1일 때만 구축. **y 좌표 = row, x 좌표 = column**인 희소 행렬.

```cpp
struct MatrixElement {
    int x, y;               // 점 좌표
    bool alloc;              // 이미 Repetition에 할당됨
    MatrixElement* right;    // 같은 row(y)의 다음 점 (→)
    MatrixElement* up;       // 같은 column(x)의 위 점 (↑)
};
```

### 구축 과정

```
입력 positions (정렬 후):
  (0,0) (100,0) (200,0) (0,100) (100,100) (200,100) (50,50)

Sparse Matrix 구조:

  y=100:  (0,100) ─→ (100,100) ─→ (200,100)
            ↑          ↑            ↑
  y=0:    (0,0)  ─→ (100,0)  ─→ (200,0)      (50,50) [isolated]

  right 포인터: 같은 row 내 → 방향 연결
  up 포인터:   HashMap<int x → topmost element>로 구축
               (같은 column이면 up 링크)
```

**불변 조건:**
- `!elem->alloc && elem->up != Null` → `!elem->up->alloc`
  (makeRepetition()은 아래→위로 탐색하므로 위쪽이 항상 미할당)

---

## 4. 알고리즘: `makeRepetition()`의 전략

### Phase 1: Sparse Matrix 스캔 (optLevel > 1)

```
nextElem → [ (0,0) → (100,0) → (200,0) ]
           [ (0,100) → (100,100) → (200,100) ]
           [ (50,50) ]
```

각 unallocated element에 대해:

```
tryArray(elem):
  1. elem에서 시작해 오른쪽/위로 grow → array 크기 측정
  2. if ncols × nrows >= MinArrayPoints(8):
       → Rep_Matrix로 인코딩, points allocated 표시
       → return true
  3. else → elem을 'points' vector로 이동 (misc points)
```

#### `tryArray()` 상세

```cpp
bool tryArray(MatrixElement* start) {
    // 1. start에서 오른쪽으로 가능한 column 수 측정
    ncols = countRightNeighbors(start);

    // 2. start에서 위로 가능한 row 수 측정
    nrows = countUpNeighbors(start);

    // 3. ncols × nrows >= 8 이면 array로 채택
    if (!arrayBigEnough(ncols * nrows))
        return false;

    // 4. Matrix parameters 계산
    //    xspace = (right.x - start.x) / ncols
    //    yspace = (up.y - start.y) / nrows
    //    xdimen = ncols, ydimen = nrows

    allocArray(start, ncols, nrows);  // 모든 점 allocated 표시
    rep.makeMatrix(ncols, nrows, xspace, yspace);
    numPoints -= ncols * nrows;
    return true;
}
```

Matrix 실패 시 → `tryHorizontalLine()` / `tryVerticalLine()`:
- 같은 row/column에 연속으로 `MinLinePoints(6)` 이상 있으면 `Rep_UniformX` 또는 `Rep_UniformY`

### Phase 2: 나머지 점 처리 (Varying / Arbitrary)

Sparse matrix에서 걸러지지 않은 점들은 `points` vector로 이동.

```
점들이 모두 같은 row(y)에 있음? → tryHorizontalRepetition()
  → Rep_VaryingX 또는 Rep_GridVaryingX

점들이 모두 같은 column(x)에 있음? → tryVerticalRepetition()
  → Rep_VaryingY 또는 Rep_GridVaryingY
  
둘 다 아님 → Rep_GridArbitrary (grid > 1)
          또는 Rep_Arbitrary (grid = 0)
```

---

## 5. 실제 예시로 보는 동작

### 예제 1: 8×5 격자 + 3개 outlier

```
positions:  (0,0) (10,0) (20,0) ... × 5 rows = 40 points
            + (99,99) (123,456) (777,888) = 3 outlier
```

```
loop 1: tryArray((0,0)) → ncols=8, nrows=5 → 40 ≥ 8 ✓
        → Rep_Matrix(8, 5, xspace=10, yspace=0)
        → pos = (0,0)

loop 2: remaining = 3 points, 모두 같은 row? 아니요
        → Rep_Arbitrary(3)  또는  Rep_GridArbitrary(3, grid=GCD(99,123,...))
        → pos = (99,99)

loop 3: empty() → 종료
```

**결과:** 2개의 OASIS element record (원래 43개 record 필요했음)

### 예제 2: 6개 점, 모두 같은 row

```
positions:  (5,0) (15,0) (25,0) (35,0) (45,0) (55,0)
grid = GCD(5,15,25,35,45,55) = 5
```

```
Phase 1: sparse matrix에서 tryArray → nrows=1 → 1×6 < 8 → 실패
  → 모든 점을 points로 이동

Phase 2: tryHorizontalRepetition() → 같은 row(y=0) ✓
  → Rep_GridVaryingX(6, grid=5)
  → deltas: (0,0) (10,0) (20,0) (30,0) (40,0) (50,0)
```

### 예제 3: optLevel=0 (최적화 없음)

```
positions: (0,0) (10,0) (20,0) (30,0) (100,100)
  → Rep_Arbitrary(5)
  → 모든 점을 델타로: (0,0) (10,0) (20,0) (30,0) (100,100)
  → zlib 압축에 의존
```

---

## 6. 임계값 (Thresholds)

```cpp
const Uint MinArrayPoints = 8;    // Matrix로 묶을 최소 점 개수
const Uint MinLinePoints  = 6;    // UniformX/Y로 묶을 최소 점 개수
```

**의미:** 하나의 element record를 추가로 생성하는 오버헤드 vs Repetition으로 절약하는 공간의 trade-off.

- 6점 미만: UniformX/Y로 묶어도 이득이 없음 → VaryingX/Y 또는 Arbitrary
- 8점 미만: Matrix로 묶어도 이득이 없음 → Line 단위로 fallback

---

## 7. grid 계산 (GCD 기반)

```cpp
grid = GCD(grid, GCD(iter->x, iter->y));  // 모든 좌표의 GCD
```

`grid > 1`이면 `Rep_GridArbitrary` 사용:
- 모든 delta가 grid의 배수임이 보장됨
- OASIS에서 `grid` 파라미터로 delta 값들을 더 압축

**한계:** OASIS의 grid는 delta에 대한 grid인데, PointGrouper는 절대좌표의 GCD를 grid로 사용. 예를 들어 점들이 (5,0), (15,0), (25,0)이면 GCD=5지만 실제 delta는 모두 10 → grid=10이 더 효율적일 수 있음. (XXX 주석: Repetition 인터페이스 개선 필요)

---

## 8. overflow 안전장치

GDSII 좌표는 int32 (-2^31 ~ 2^31-1). OASIS delta로 변환 시 overflow 가능:

```cpp
bool PointInReach(const Delta& from, const Delta& to) {
    if (sizeof(long) > 4) return true;  // 64-bit: 항상 안전
    return (CoordInReach(from.x, to.x) && CoordInReach(from.y, to.y));
}
```

delta가 overflow 위험이 있으면 해당 점을 Repetition에서 제외.

---

## 9. 상대좌표 모드와의 협력

PointGrouper가 반환한 `pos`(origin)는 `GdsToOasisConverter::writeXxx()`에서 `placementPos`/`textPos`/`geometryPos`와 비교됨:

```cpp
// writeRectangles() 내부
PointGrouper pg(rect->positions, optLevel, deleteDuplicates);
while (!pg.empty()) {
    const Repetition* rep = pg.makeRepetition(&pos);
    if (relativeMode && !PointInReach(geometryPos, pos))
        creator.setXYrelative(false);  // 일시적 절대모드
    creator.beginRectangle(..., pos.x, pos.y, ..., rep);
    geometryPos.assign(pos.x, pos.y);
    creator.setXYrelative(relativeMode);
}
```

---

## 10. PointGrouper vs AREF

PointGrouper가 필요 없는 유일한 element 타입: **AREF**

이유:
- AREF는 이미 GDSII 단계에서 격자 배열로 정의됨
- `ArefInfo`는 자체 `Repetition rep` 멤버를 이미 가지고 있음
- AREF는 병합(merge)되지 않으므로 positions vector가 없음
- `writeArefs()`는 PointGrouper 없이 바로 `&aref->rep`을 OasisCreator에 전달

---

## 11. 요약: 전체 최적화 파이프라인

```
GDSII file
    ↓ GdsParser
GdsToOasisConverter::beginBoundary(batch)
    ↓ identical element merge (ElementManager)
SrefInfo{RECT, 50x50, layer=1, positions=850points}
    ↓ 정렬 + 중복 제거
    ↓ PointGrouper (optLevel=2)
        ↓ tryArray  → Rep_Matrix(20×40, xspace=100, yspace=100) → 1 record
        ↓ tryArray  → Rep_Matrix(5×5, xspace=50, yspace=50)    → 1 record
        ↓ tryHorizontalRepetition  → Rep_GridVaryingX(3)        → 1 record
        ↓ Rep_Arbitrary(7)                                       → 1 record
    = 4 OASIS element records  (원래 850개 → 4개, 99.5% 감소!)
    ↓
OasisCreator → OASIS binary file
```
