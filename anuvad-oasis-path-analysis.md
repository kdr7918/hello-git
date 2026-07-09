# OASIS Path — 기하학 정의와 중심선 정점(점열) 계산

> 대상 소스: `src/oasis/records.h` (PathRecord), `src/oasis/element.h` (PathElem),
> `src/oasis/element.cc` (decodePointList), `src/oasis/oasis.h` (PointList)
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09
> 집중: 기하학 정의 + 중심선 정점(점열) 계산 로직 (코드 포함 상세 설명)
> (Trapezoid 분석 문서와 동일한 구성으로 작성)

---

## 1. 기하학 정의 (OASIS spec §24 / Path Record)

OASIS **PATH** 레코드는 일정한 반폭(half-width)을 가진 **중심선(centerline)**
을 따라 그려지는 선분 도형입니다. 트랜지스터 게이트, 금속 배선 등细长
도형을 compact하게 표현하는 데 쓰입니다.

### 1.1 파라미터 구성

| 필드 | 타입 | 의미 |
|------|------|------|
| `layer` / `datatype` | Ulong | 레이어 / 데이터타입 |
| `halfwidth` | Ulong | 중심선 양측으로 뻗는 폭의 **절반** (전체 폭 = 2×halfwidth) |
| `startExtn` | long | 시작점 바깥으로 연장되는 길이 (extension) |
| `endExtn` | long | 끝점 바깥으로 연장되는 길이 |
| `x, y` | long | 중심선 **첫 번째 점(anchor)**의 절대 좌표 |
| `ptlist` | PointList | 첫 점 이후의 상대 변위(delta) 열 |

기하학상 Path는 중심선 점열 `P0, P1, ..., Pn`로 정의되며, 각 선분은
양측으로 `halfwidth`만큼 평행이동한 외곽 폴리곤으로 렌더링됩니다.

```
            halfwidth
        <-------------->
        ┌──────────────┐   ┐
   P0 ──┤  centerline ├──┤   │
        └──────────────┘   ┘ halfwidth
              │
            P1 ... Pn

   시작점: P0 = (x, y)         절대 좌표
   이후점: Pk = P0 + Σ delta_i  (상대 누적)
```

### 1.2 중심선 vs 외곽 폴리곤

- **중심선(centerline)**: `decodePointList()`가 돌려주는 점열 — 도형의
  "뼈대".
- **외곽 폴리곤(outline)**: 중심선 각 선분을 법선 방향으로 `±halfwidth`만큼
  평행이동해 만든 닫힌 다각형. 실제 채우기(fill)는 이 폴리곤으로 수행.
- **Extension**: `startExtn`/`endExtn`이 0이 아니면 중심선 끝점을 그 길이만큼
  연장한 뒤 외곽을 구성합니다(spec §24.3).

> 본 문서는 **중심선 정점(점열) 계산** — 즉 `PointList` 디코딩 — 에 집중하며,
> 외곽 폴리곤 확장은 renderer/builder 책임입니다.

### 1.3 불변 / 제약

- `halfwidth >= 0`. `halfwidth == 0`이면 0폭 선(렌더링 시 invisible 또는
  application-dependent).
- `ptlist`는 최소 1개 이상의 점을 가져야 함(P0는 항상 존재, delta 열은
  0개 이상).
- 중심선 점들은 **Manhattan / Octangular / AllAngle** 제약을 가질 수 있음
  (다음 절의 ListType).

---

## 2. 데이터 구조

### 2.1 `PathRecord` (파일 레코드, `records.h`)

```cpp
struct PathRecord : public OasisRecord {
    int         infoByte;
    Ulong       extnScheme;
    Ulong       layer;
    Ulong       datatype;
    Ulong       halfwidth;
    long        startExtn;
    long        endExtn;
    long        x, y;
    PointList   ptlist;
    RawRepetition  rawrep;

    PathRecord();
    virtual ~PathRecord();
};
```

`infoByte`는 halfwidth/extension 유무와 인코딩 방식을, `extnScheme`은
start/end extension의 부호 규칙을 인코딩합니다(`creator.cc` 참조).

### 2.2 `PathElem` (빌드된 요소, `element.h`)

```cpp
struct PathElem : public Element {
    long     x, y;
    long     halfwidth;
    long     startExtn, endExtn;
    Blob     ptlistBlob;       // raw point-list bytes (OASIS binary encoding)
    Box      box;              // cached bounding box (computed at build time)

    PathElem() : x(0), y(0),
                 halfwidth(0), startExtn(0), endExtn(0) { }

    PointList  decodePointList () const;
    Box        boundingBox () const { return box; }
};
```

빌드 단계에서는 점열을 **압축 바이너리(`Blob`)로 보관**하고, 필요할 때
`decodePointList()`로 `PointList`(절대/상대 Delta 벡터)로 풉니다. `box`는
빌드 시 계산된 bounding box 캐시입니다.

---

## 3. 중심선 정점(점열) 계산 — `decodePointList()`

`PathElem::decodePointList()`는 OASIS 바이너리 point-list를 읽어
`PointList`(Delta 벡터열)로 복원합니다. 반환된 `PointList`의 각 `Delta`는
**첫 점(P0) 이후의 상대 변위**입니다(절대 좌표는 `P0=(x,y)`에 누적 합산).

### 3.1 전체 소스 코드

아래는 `element.cc`의 실제 구현입니다.

```cpp
PointList
PathElem::decodePointList () const {
    if (ptlistBlob.empty()) return PointList();

    BlobReader  reader(ptlistBlob.data, ptlistBlob.data + ptlistBlob.size);
    Ulong  type = reader.readUInt();
    Ulong  numDeltas = reader.readUInt();

    PointList  ptlist(static_cast<PointList::ListType>(type));

    switch (static_cast<PointList::ListType>(type)) {
        case PointList::ManhattanHorizFirst:
        case PointList::ManhattanVertFirst:
            for (Ulong i = 0; i < numDeltas; ++i)
                ptlist.addPoint(Delta(reader.readSInt(), 0));
            break;

        case PointList::Manhattan: {
            for (Ulong i = 0; i < numDeltas; ++i) {
                Ulong  raw = reader.readUInt();
                Delta::Direction  dirn =
                    static_cast<Delta::Direction>(raw & 0x3);
                Ulong  mag = raw >> 2;
                ptlist.addPoint(Delta(dirn, mag));
            }
            break;
        }

        case PointList::Octangular: {
            for (Ulong i = 0; i < numDeltas; ++i) {
                Ulong  raw = reader.readUInt();
                Delta::Direction  dirn =
                    static_cast<Delta::Direction>(raw & 0x7);
                Ulong  mag = raw >> 3;
                ptlist.addPoint(Delta(dirn, mag));
            }
            break;
        }

        case PointList::AllAngle:
        case PointList::AllAngleDoubleDelta:
            for (Ulong i = 0; i < numDeltas; ++i)
                ptlist.addPoint(reader.readGDelta());
            break;
    }

    return ptlist;
}
```

### 3.2 디코딩 단계별 상세 설명

1. **BlobReader 구성**: `ptlistBlob`의 시작/끝 포인터로 varint 리더 생성.
   빈 Blob이면 빈 `PointList` 반환.
2. **type 읽기**: 맨 앞 varint가 `ListType`(spec Table 7-7).
3. **numDeltas 읽기**: 중심선 상 두 번째 점부터 끝점까지의 변위 개수.
   → 전체 중심선 점 수 = `numDeltas + 1` (P0 포함).
4. **ListType별 점 복원**: 아래 표 참조.

### 3.3 `PointList::ListType` — 기하 인코딩 방식

| ListType | 값 | 기하 제약 | 점 인코딩 |
|----------|----|-----------|-----------|
| `ManhattanHorizFirst` | 0 | 수평-수직 교번, **첫 변위는 수평** | `readSInt()` → (Δx, 0) |
| `ManhattanVertFirst`  | 1 | 수평-수직 교번, **첫 변위는 수직** | `readSInt()` → (Δx, 0) |
| `Manhattan`           | 2 | 8방향 중 4 Manhattan 방향 | 2비트 방향 + 크기(`>>2`) |
| `Octangular`          | 3 | 8방향(octangular) | 3비트 방향 + 크기(`>>3`) |
| `AllAngle`            | 4 | 자유 각도 | `readGDelta()` (x,y 모두) |
| `AllAngleDoubleDelta` | 5 | 자유 각도, 2×정밀도 | `readGDelta()` |

**핵심 관찰:**

- **ManhattanHorizFirst / ManhattanVertFirst** (type 0,1): 교번(alternating)
  인코딩입니다. 첫 변위는 항상 수평(HorizFirst) 또는 수직(VertFirst)이고,
  이후 변위는 번갈아 방향이 바뀝니다. 코드에서는 단순히 `(readSInt(), 0)`로
  읽는데, **실제 방향 교번은 디코딩 후 점들을 연결할 때 적용**됩니다
  (여기서는 x 변위만 읽고 y=0으로 저장 — caller가 Horiz/VERT 플래그로
  교번 복원). 주석/사용부에서 방향 플래그를 따릅니다.
- **Manhattan** (type 2): 각 점을 `raw`로 읽어 하위 2비트를 `Direction`
  (E/W/N/S), 상위를 크기로 해석. → 좌표계 방향 단위 벡터 × 크기.
- **Octangular** (type 3): 하위 3비트가 8방향(수평/수직 + 4 대각선),
  상위를 크기. 대각선은 45° 제약.
- **AllAngle** (type 4,5): 완전 자유 각도. `readGDelta()`가
  (x변위, y변위) 쌍을 그대로 읽습니다. type 5는 배정밀도 표현(더 촘촘한
  그리드).

### 3.4 `Delta` 와 `readGDelta`

`Delta`(`oasis.h`)는 부호 있는 2D 변위 구조체입니다. `readGDelta()`는
OASIS의 G(general) Delta 인코딩을 풀어줍니다:

```cpp
Delta  readGDelta () {
    Ulong  val = readUInt();
    if ((val & 0x1) == 0) {                    // 방향 인코딩
        Delta::Direction  dirn =
            static_cast<Delta::Direction>((val >> 1) & 0x7);
        return Delta(dirn, val >> 4);
    }
    bool  isNeg = (val & 0x2);                 // 자유 (x,y) 인코딩
    long  xdisp = (isNeg ? -(val >> 2) : (val >> 2));
    long  ydisp = readSInt();
    return Delta(xdisp, ydisp);
}
```

즉, 대부분의 점은 방향+크기로 압축되고, 가끔 자유 (x,y) 쌍이 섞입니다.

### 3.5 절대 좌표 환원 (중심선 점 계산)

`decodePointList()`가 돌려준 `Delta` 열은 **P0 이후 상대 변위**입니다.
절대 중심선 좌표는 다음과 같이 누적합니다:

```cpp
// P0 = 절대 시작점
Delta  P0(x, y);                      // PathRecord/PathElem의 x, y
PointList  pl = pathElem.decodePointList();

vector<Delta>  centerline;
centerline.push_back(P0);
Delta  cur = P0;
for (const Delta& d : pl) {           // d = 상대 변위
    cur += d;                         // 누적 합산 (Delta::operator+=, 오버플로우 검사)
    centerline.push_back(cur);
}
// centerline[] = P0, P1, ..., Pn  (중심선 정점 열)
```

### 3.6 예시 (ManhattanHorizFirst, halfwidth=5)

중심선 delta 열이 `(10,0), (0,20), (-10,0)` 이고 `P0=(0,0)`이라면:

```
P0 = (0, 0)
P1 = (10, 0)    Δx=+10 (수평)
P2 = (10, 20)   Δy=+20 (수직)
P3 = (0, 20)    Δx=-10 (수평)

외곽 폴리곤은 각 선분을 y방향 ±5 (halfwidth) 로 평행이동해 생성:
  상단: (0,5)-(10,5)-(10,25)-(0,25)
  하단: (0,-5)-(10,-5)-(10,15)-(0,15)
  → 닫힌 사각 띠 폴리곤
```

---

## 4. 요약

- **Path**는 중심선 점열 + `halfwidth` + `startExtn`/`endExtn`으로 정의되는
  선분 도형(OASIS spec §24).
- **중심선 정점(점열) 계산**은 `PathElem::decodePointList()`가 수행합니다:
  OASIS 바이너리 Blob → `PointList`(상대 Delta 열).
- `PointList`는 6가지 `ListType`(Manhattan / Octangular / AllAngle 등)으로
  기하 제약별 압축 인코딩을 지원하며, type 0/1은 수평/수직 교번,
  type 2/3은 방향+크기, type 4/5는 자유 (x,y).
- 절대 중심선 좌표는 `P0=(x,y)`에 Delta 열을 누적 합산해 얻습니다.
- 실제 폴리곤 렌더링(채우기)은 중심선을 `±halfwidth`로 평행이동 확장하는
  별도 단계(renderer/builder)에서 이루어집니다.

---

## 참고: 관련 소스 위치

- `src/oasis/records.h` — `PathRecord` 정의
- `src/oasis/element.h` — `PathElem` 정의 (`decodePointList` 선언)
- `src/oasis/element.cc` — `PathElem::decodePointList()` 구현 (위 인용),
  `BlobReader` / `readGDelta`
- `src/oasis/oasis.h` — `PointList` 클래스, `ListType` 열거, `Delta` 구조체
- `src/oasis/creator.cc` — Path 레코드 기록 시 `halfwidth`/`extnScheme` 처리
