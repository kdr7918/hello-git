# OASIS PointList — 기하학적 의미

> 대상 소스: `src/oasis/oasis.h` (PointList 클래스, ListType 열거),
> `src/oasis/element.cc` (PolygonElem/PathElem::decodePointList)
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09
> 집중: PointList의 기하학적 의미와 점열 인코딩/디코딩

---

## 1. 기하학적 의미

OASIS에서 **POINT LIST**는 폴리곤(Polygon)이나 경로(Path)의 **꼭짓점 열**
을 표현하는 공통 컨테이너입니다. 첫 점은 절대 좌표(`x, y`)로 주어지고,
이후 점들은 **첫 점으로부터의 상대 변위(Delta)** 로 인코딩됩니다.

기하학적으로 PointList는 다음을 정의합니다:

- **연결 순서**: 점들은 주어진 순서대로 연결되어 폴리곤 경계 또는 중심선을
  이룹니다.
- **상대 표현**: 각 점은 "이전 점에서 얼마나 이동했는가"로 저장되므로,
  도형이 원점에서 멀리 있더라도 **변위 값 자체는 작게** 유지되어 파일이
  압축됩니다.
- **각도 제약(Angle constraint)**: ListType에 따라 인접 변의 방향이
  Manhattan(수평/수직만), Octangular(45° 포함), AllAngle(자유)로 제한됩니다.
  이 제약은 레이아웃 제조 공정(광학 근사, OPC)과 직결되는 기하학 속성입니다.

### 1.1 절대 좌표 환원 공식

점열 `Δ₀, Δ₁, ..., Δₙ₋₁` (각각 상대 변위)와 앵커 `P₀=(x,y)`일 때:

```
P₀ = (x, y)
Pₖ = P₀ + Σ_{i=0}^{k-1} Δᵢ      (k = 1..n)
```

즉 PointList는 기하학적으로 **"앵커 + 누적 변위"로 정의되는 점들의 순서쌍**
입니다.

---

## 2. `ListType` — 기하학 인코딩 방식 (spec Table 7-7)

`PointList::ListType`은 점들이 가질 수 있는 **방향 제약**을 규정합니다.
OASIS는 이를利用해 변위를 매우 적은 비트로 부호화합니다.

```cpp
enum ListType {
    ManhattanHorizFirst  = 0,
    ManhattanVertFirst   = 1,
    Manhattan            = 2,
    Octangular           = 3,
    AllAngle             = 4,
    AllAngleDoubleDelta  = 5,
    MaxListType          = 5
};
```

| ListType | 값 | 기하 제약 | 인코딩 방식 |
|----------|----|-----------|-------------|
| `ManhattanHorizFirst` | 0 | 수평-수직 **교번**, 첫 변위는 **수평** | 각 변위 = `readSInt()` → (Δx, 0). 방향은 교번 규칙으로 복원 |
| `ManhattanVertFirst`  | 1 | 수평-수직 **교번**, 첫 변위는 **수직** | 동일, 단 첫 변위가 수직 |
| `Manhattan`           | 2 | 4방향(E/W/N/S)만 허용 | 2비트 방향 + 크기(`raw >> 2`) |
| `Octangular`          | 3 | 8방향(수평/수직 + 4대각선 45°) | 3비트 방향 + 크기(`raw >> 3`) |
| `AllAngle`            | 4 | 자유 각도(임의 방향) | `readGDelta()` → (x, y) 쌍 |
| `AllAngleDoubleDelta` | 5 | 자유 각도, 2×정밀도 그리드 | `readGDelta()` (배정밀도) |

### 2.1 기하학적 해석

- **Manhattan (type 0,1,2)**: 모든 변은 수평 또는 수직. 직각 다각형.
  광학 노광에서 가장 쉽게 제조되는 형태. type 0/1은 **방향이 번갈아
  바뀌는 것**이 보장되므로 한 좌표만 저장해도 됨(교번 압축).
- **Octangular (type 3)**: 45° 대각선을 포함. 사선 배선 가능. 방향을
  3비트(8종)로 표현.
- **AllAngle (type 4,5)**: 제약 없음. 임의 각도. 가장 유연하지만 가장
  많은 비트 소모. type 5는 그리드 배율이 높아 미세한 각도 표현.

---

## 3. 디코딩 코드 (상세)

`PolygonElem::decodePointList()`와 `PathElem::decodePointList()`는 동일
구조입니다(`element.cc`). 아래는 Path 버전입니다.

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

### 3.1 단계별 설명

1. **BlobReader**: `ptlistBlob` 바이너리를 varint 리더로 감쌉.
2. **type 읽기**: 첫 varint = ListType.
3. **numDeltas 읽기**: 앵커 이후 상대 변위 개수. 전체 점 수 = `numDeltas + 1`.
4. **ListType 분기**:
   - type 0/1: `readSInt()`로 x변위만 읽고 y=0. (교번이므로 caller가
     Horiz/VERT 플래그로 방향 복원 — 실제 점 연결 시 번갈아 적용)
   - type 2: `raw` 하위 2비트 = 방향(E/W/N/S), 상위 = 크기 → `Delta(dirn, mag)`
   - type 3: 하위 3비트 = 8방향, 상위 = 크기.
   - type 4/5: `readGDelta()`가 (x, y) 자유 쌍을 읽음.

### 3.2 `Delta` 와 `readGDelta`

`Delta`는 부호 있는 2D 변위. `readGDelta()`는 OASIS G-Delta 인코딩:

```cpp
Delta  readGDelta () {
    Ulong  val = readUInt();
    if ((val & 0x1) == 0) {                    // 방향+크기 압축
        Delta::Direction  dirn =
            static_cast<Delta::Direction>((val >> 1) & 0x7);
        return Delta(dirn, val >> 4);
    }
    bool  isNeg = (val & 0x2);                 // 자유 (x,y) 쌍
    long  xdisp = (isNeg ? -(val >> 2) : (val >> 2));
    long  ydisp = readSInt();
    return Delta(xdisp, ydisp);
}
```

대부분 점은 방향+크기로 압축되고, 가끔 자유 (x,y) 쌍이 섞입니다.

---

## 4. 예시 (Octangular, anchor P0=(0,0))

raw 열이 `[ (3<<3)|5, (1<<3)|10 ]` (방향=3即NE, 크기=5; 방향=1即E, 크기=10)라면:

```
Δ₀ = NE·5  = (+5, +5)
Δ₁ = E·10  = (+10, 0)

P₀ = (0, 0)
P₁ = (0,0) + (5,5)  = (5, 5)
P₂ = (5,5) + (10,0) = (15, 5)
```

45° 대각선 후 수평 이동하는 꺾인 선 — Octangular 제약에 부합.

---

## 5. 요약

- **PointList**는 폴리곤/경로의 꼭짓점 열을 **앵커 + 상대 변위**로 표현.
- **ListType**이 기하학 각도 제약(Manhattan/Octangular/AllAngle)을 규정하며,
  이 제약이 인코딩 비트 수를 결정합니다(제조 공정 관련 핵심 속성).
- 디코딩은 `decodePointList()`가 수행하며, type별로 방향 압축 또는 자유
  (x,y) 쌍을 풉니다.
- 절대 좌표는 앵커에 변위를 누적 합산해 얻습니다.

---

## 참고: 관련 소스 위치

- `src/oasis/oasis.h` — `PointList` 클래스, `ListType` 열거, `Delta` 구조체
- `src/oasis/element.h` — `PolygonElem`, `PathElem` (`decodePointList` 선언)
- `src/oasis/element.cc` — `decodePointList()` 구현, `BlobReader`, `readGDelta`
