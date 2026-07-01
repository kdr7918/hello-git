# Anuvad GDSII→OASIS Converter (`conv/gds-oasis`) 상세 분석

> Anuvad 0.10 — SoftJin Technologies
> 파일 위치: `src/conv/gds-oasis.h` + `gds-oasis.cc` (3224줄)

---

## 1. 외부 인터페이스 (`gds-oasis.h`)

```cpp
namespace GdsiiOasis {

struct GdsToOasisOptions {
    FileHandle::FileType gdsFileType;   // 자동/GDSII/gzip
    Validation::Scheme   valScheme;     // OASIS 검증: Checksum32 권장
    bool   compress;                    // CBLOCK 압축 (권장: true)
    bool   immediateNames;              // name table 모드 (권장: false = strict)
    bool   deleteDuplicates;            // 중복 element 제거 (권장: true)
    bool   pathToRectangle;             // 단일 segment PATH→RECTANGLE 변환
    bool   relativeMode;                // 상대 좌표 모드 사용
    bool   transformTexts;              // transform된 TEXT→PLACEMENT 변환
    bool   verbose;                     // 진행 메시지 출력
    Uint   optLevel;                    // 최적화 레벨 (0, 1, >1)
};

void ConvertGdsToOasis (const char* infilename,
                         const char* outfilename,
                         WarningHandler warner,
                         const GdsToOasisOptions& options);

}
```

**동작:** `GdsParser` + `GdsToOasisConverter`(Builder) + `OasisCreator`를 연결.

```
GDSII file → GdsParser → GdsToOasisConverter → OasisCreator → OASIS file
                          (GdsBuilder 상속)       (OasisBuilder 상속)
```

---

## 2. 주요 상수

```cpp
const char   OasisVersion[] = "1.0";
const Uint   MaxBufferedElements = 300 * 1000;
```

`MaxBufferedElements`: 한 번에 버퍼링할 element 수. 채워지면 `writeElements()`로 OASIS에 flush. 최적화 레벨이 높을수록 더 많이 버퍼링 → modal variable 활용도 ↑.

---

## 3. 보조 자료구조

### 3.1 `PropInfo` / `PropInfoList`

GDSII property (PROPATTR + PROPVALUE)를 버퍼링하기 위한 구조.

```cpp
struct PropInfo {
    int              attr;      // PROPATTR (1~127)
    PropStringIndex  value;     // allPropStrings 벡터 인덱스
    size_t getHash() const;
    bool operator== (const PropInfo&) const;
};

// attr 기준 정렬된 list. 동일 element 탐지를 위해 hash + == 지원.
class PropInfoList { list<PropInfo> plist; ... };
```

- OASIS 표준 property가 아닌 GDSII property는 `S_GDS_PROPERTY`로 변환됨.
- `PropStringIndex`는 32비트 인덱스 (포인터 대신 → 64비트 시스템에서 메모리 절약).

### 3.2 `TransformInfo`

GDSII transform (STRANS + MAG + ANGLE)을 압축 저장.

```cpp
class TransformInfo {
    double mag;     // 음수 => flip(reflectX) 비트 켜짐
    double angle;   // degrees, counter-clockwise
};
```

- `mag` 부호로 flip 표현 → 1비트를 위해 8바이트 낭비 방지
- `isIdentity()`: angle=0, mag=1, flip=false → OASIS PLACEMENT에서 생략 가능

---

## 4. ElementInfo 계층 (Internal Element Buffer)

### 4.1 기반 클래스

```cpp
struct ElementInfo {
    PropInfoList propList;    // 모든 element 타입이 공유
};
```

### 4.2 `SrefInfo` — GDSII SREF → OASIS PLACEMENT

```cpp
struct SrefInfo : ElementInfo {
    vector<GdsPoint> positions;   // 동일한 SREF가 발견된 모든 위치
    CellNameIndex    cellIndex;   // 참조할 CellName 인덱스
    TransformInfo    transform;   // MAG+ANGLE+flip
};
```

- **hash+== 비교 시 positions 제외** → 동일 cell+transform+Sref는 positions만 모아서 **하나의 PLACEMENT + Repetition**으로 병합.
- `operator<`: cellIndex 기준 정렬 → modal variable `placementCell` 활용 극대화.

### 4.3 `ArefInfo` — GDSII AREF → OASIS PLACEMENT + Repetition(Matrix)

```cpp
struct ArefInfo : ElementInfo {
    GdsPoint       pos;         // 단일 origin 위치
    CellNameIndex  cellIndex;
    TransformInfo  transform;
    Repetition     rep;         // OASIS Matrix Repetition
};
```

- **positions 벡터 없음** — AREF는 자체 Repetition을 가지므로 병합 불필요.
- `getHash()`/`operator==` 불필요 (병합 안 함).

### 4.5 `RectangleInfo` — GDSII BOUNDARY/BOX → OASIS RECTANGLE

```cpp
struct RectangleInfo : ElementInfo {
    vector<GdsPoint> positions;   // 동일 rect의 모든 위치
    short  layer, datatype;
    int    width, height;
};
```

- 4-point BOUNDARY나 BOX에서 추출. `tryTrapezoid()` 실패 시 fallback.
- `operator<`: layer → width 순으로 정렬 → modal layer/datatype/width/height 활용.

### 4.6 `TrapezoidInfo` — GDSII BOUNDARY → OASIS TRAPEZOID

```cpp
struct TrapezoidInfo : ElementInfo {
    vector<GdsPoint> positions;
    short  layer, datatype;
    int    width, height;
    int    delta_a, delta_b;
    Oasis::Trapezoid::Orientation orient;   // HORIZONTAL or VERTICAL
};
```

- `tryTrapezoid()`가 4-point BOUNDARY를 사다리꼴로 인식 가능한지 판별.
- 6가지 사다리꼴 패턴 검사 (`tryVerticalTrapezoid`, `tryHorizontalTrapezoid`).

### 4.7 `PolygonInfo` — GDSII BOUNDARY → OASIS POLYGON

```cpp
struct PolygonInfo : ElementInfo {
    vector<GdsPoint> positions;
    short  layer, datatype;
    PointList ptlist;         // OASIS PointList (delta 인코딩)
};
```

- RECTANGLE/TRAPEZOID 변환이 실패한 BOUNDARY의 최종 fallback.
- PointList의 type(1/2/3/4-delta)까지 hash에 포함.

### 4.8 `PathInfo` — GDSII PATH → OASIS PATH

```cpp
struct PathInfo : ElementInfo {
    vector<GdsPoint> positions;
    short  layer, datatype;
    int    halfwidth;      // GDSII width/2
    int    startExtn, endExtn;   // GDSII pathtype 변환 결과
    PointList ptlist;
};
```

- `pathToRectangle()`: 단일 segment PATH를 RECTANGLE로 변환 (옵션).

### 4.9 `TextInfo` — GDSII TEXT (untransformed) → OASIS TEXT

```cpp
struct TextInfo : ElementInfo {
    vector<GdsPoint> positions;
    TextStringIndex  textIndex;
    short  layer, texttype;
    int    width;
    Uchar  pathtype;        // GDSII Pathtype
    Uchar  presentation;    // bit-packed: font(2) | vjust(2) | hjust(2)
};
```

- GDSII TEXT는 `setPresentation(font, vjust, hjust)`를 OASIS property(`GDS_PRESENTATION`)로 저장.
- `GDS_PATHTYPE`, `GDS_WIDTH`도 별도 property로 저장.

### 4.10 `XTextInfo` — GDSII TEXT (transformed) → OASIS PLACEMENT + special cell

```cpp
struct XTextInfo : public TextInfo {
    vector<GdsPoint> positions;
    TransformInfo    transform;
    CellNameIndex    cellIndex;    // special cell 참조
};
```

- transform이 있는 GDSII TEXT는 OASIS TEXT가 직접 transform을 지원하지 않으므로, **TEXT 하나만 담은 special cell**을 생성하고 현재 cell에서는 `PLACEMENT of special_cell`로 변환.
- `cellIndex`는 `writeTransformedTexts()`에서 설정됨.
- **2-pass 처리**: pass 1 → PLACEMENT 기록, pass 2 → special cell의 TEXT 기록.

---

## 5. `ElementManager<ElemType>` 템플릿

```cpp
template <typename ElemType>
class ElementManager {
    PointerVector<ElemType> elemVec;    // 소유권 + sort + iteration
    HashSet<ElemType*>      elemSet;    // 중복 탐지 (hash + ==)
public:
    void add(ElemType* elem, const GdsPoint& pos);
    void sort();               // modal variable 최적화를 위한 정렬
    void deleteAll();
    size_t numUniqueElements() const;
};
```

**동작:**
1. `add(elem, pos)`: elemSet에서 동일 element 검색. 있으면 → 기존 element의 positions에 pos만 추가 (병합). 없으면 → elemVec + elemSet에 삽입.
2. `sort()`: `operator<` 기준 정렬. **목적: OASIS modal variable 재사용 극대화.**
3. flush 시: 정렬된 순서로 하나씩 OasisCreator 메서드 호출.

**예외:** ArefManager는 병합하지 않음 → 별도 단순 리스트.

---

## 6. `GdsToOasisConverter` 클래스 (핵심)

### 6.1 Builder callbacks 파이프라인

```
GdsParser → GdsToOasisConverter::*
               ↓ GDSII element buffer
               ↓ writeElements()
               ↓ OasisCreator::*
                   ↓ OasisWriter
                       ↓ OASIS binary file
```

### 6.2 Element Type enum

```cpp
enum ElementType {
    ElemSref, ElemAref, ElemNormalText,
    ElemTransformedText, ElemRectangle,
    ElemTrapezoid, ElemPolygon, ElemPath
};
```

### 6.3 주요 멤버

| 멤버 | 설명 |
|---|---|
| `creator` | OasisCreator — OASIS 파일 기록 |
| `currElement`, `currElemType` | 현재 GDSII element가 변환된 buffer element |
| `currentCell` | 현재 처리 중인 cell의 allCellNames 인덱스 |
| `srefMgr` ~ `pathMgr` | 8개의 ElementManager 인스턴스 |
| `cellNameMap`, `allCellNames` | 구조체 이름 → CellName* 매핑 |
| `textStringMap`, `allTextStrings` | 문자열 → TextString* 매핑 |
| `propStringMap`, `allPropStrings` | property 값 → PropString* 매핑 |
| `xtexts` | transformed text 임시 보관 |

### 6.4 Builder Callback 구현

#### `beginLibrary()`

```cpp
void beginLibrary(name, modTime, accTime, units, options) {
    creator.beginFile(OasisVersion, units.dbToMeter, options.valScheme);
    // units: GDSII dbToMeter → OASIS unit (Oreal)
    // OASIS는 valScheme만 END record에서 사용
}
```

- GDSII의 `modTime`/`accTime`는 OASIS에 **저장되지 않음** (포맷 차이).

#### `beginStructure(name, createTime, modTime, options)`

```cpp
void beginStructure(name, createTime, modTime, options) {
    flushElements();                   // 이전 cell의 버퍼 비움
    CellName* cn = makeCellName(name); // name table 등록
    creator.beginCell(cn);             // OASIS cell 시작
}
```

#### `endStructure()`

```cpp
void endStructure() {
    writeElements();    // 버퍼링된 모든 element flush
    writeTextCells();   // special text cells 기록
    creator.endCell();
}
```

#### `beginBoundary(layer, datatype, points, options)`

```cpp
void beginBoundary(layer, datatype, points, options) {
    GdsPoint pos;
    TrapezoidInfo trap;
    if (tryTrapezoid(points, &trap, &pos)) {
        trapMgr.add(new TrapezoidInfo(trap), pos);     // → TRAPEZOID
    } else {
        int w, h;
        if (tryGetRect(points, &w, &h, &pos)) {
            rectMgr.add(new RectangleInfo(...), pos);   // → RECTANGLE
        } else {
            polygonMgr.add(new PolygonInfo(...), pos);  // → POLYGON
        }
    }
}
```

**변환 우선순위: Trapezoid → Rectangle → Polygon**

#### `beginPath(layer, datatype, points, options)`

옵션 `pathToRectangle`이 true이고 단일 segment PATH면 → RECTANGLE로 변환.
아니면 → PathInfo를 pathMgr에 추가.

#### `beginSref(sname, x, y, strans, options)`

```cpp
void beginSref(sname, x, y, strans, options) {
    srefMgr.add(new SrefInfo(cellIndex, transform), GdsPoint(x,y));
}
```

#### `beginAref(sname, cols, rows, points, strans, options)`

3-point GDSII AREF를 OASIS Matrix Repetition으로 변환 후 arefMgr에 추가.

#### `beginNode(layer, nodetype, points, options)`

GDSII NODE는 OASIS에 대응 element 없음 → **경고 메시지** 출력 후 무시.

#### `beginBox(layer, boxtype, points, options)`

5-point BOX를 4-point RECTANGLE로 변환 후 rectMgr에 추가.

#### `beginText(layer, texttype, x, y, text, strans, options)`

- transform이 없거나 `transformTexts=false` → TextInfo → textMgr
- transform이 있고 `transformTexts=true` → XTextInfo → xtextMgr

#### `addProperty(attr, value)`

현재 `currElement`의 `propList`에 추가.

#### `endElement()`

`currElement`를 적절한 ElementManager에 add → `numBufferedElements` 증가 → `MaxBufferedElements` 초과 시 `writeElements()`.

---

### 6.5 Write Pipeline

```
writeElements()
  ├── beginCell()            // modal 변수 초기화
  ├── writeSrefs()           // 정렬된 SrefInfo → OasisCreator::beginPlacement
  ├── writeArefs()           // ArefInfo → beginPlacement + Repetition(Matrix)
  ├── writeNormalTexts()     // TextInfo → beginText + GDS_PRESENTATION/GDS_PATHTYPE property
  ├── writeTransformedTexts()// XTextInfo → beginPlacement (special cell ref)
  ├── writeRectangles()      // RectangleInfo → beginRectangle
  ├── writeTrapezoids()      // TrapezoidInfo → beginTrapezoid
  ├── writePolygons()        // PolygonInfo → beginPolygon
  ├── writePaths()           // PathInfo → beginPath
  └── writeTextCells()       // XTextInfo special cell → TEXT element 기록
```

**최적화 기법:**
1. **Element 병합** — 동일한 element는 하나로 합치고 positions를 Repetition으로 인코딩
2. **Modal variable 정렬** — `operator<`로 정렬 → OASIS modal 변수 재사용
3. **상대 좌표 모드** — `relativeMode=true`면 XYRELATIVE 사용하여 delta 인코딩

---

## 7. GDSII→OASIS Element Mapping

| GDSII | OASIS | 비고 |
|---|---|---|
| BOUNDARY (4점 직사각형) | RECTANGLE | tryGetRect 성공 시 |
| BOUNDARY (4점 사다리꼴) | TRAPEZOID | tryTrapezoid 성공 시 |
| BOUNDARY (기타) | POLYGON | 최종 fallback |
| BOX | RECTANGLE | 5점→4점 단순화 |
| PATH | PATH (또는 RECTANGLE) | 단일 segment 옵션 |
| SREF | PLACEMENT | 동일 SREF 병합 → Repetition |
| AREF | PLACEMENT + Matrix Rep | 3-point → xdimen/ydimen/xspace/yspace |
| TEXT (untransformed) | TEXT + GDS_* properties | PROPVALUE로 GDSII 전용 필드 저장 |
| TEXT (transformed) | PLACEMENT + special cell TEXT | 2-pass |
| NODE | **무시** (경고) | OASIS 대응 없음 |
| Property | Element Property (`S_GDS_PROPERTY`) | PROPATTR→GDSII property 이름 사용 |

---

## 8. 통합 Builder 관점에서의 설계 시사점

### 8.1 GdsToOasisConverter가 보여주는 패턴

**버퍼링 + 병합 + Repetition 변환**이 GDSII→OASIS 컨버터의 핵심입니다. 이 패턴은 통합 Builder 설계에도 그대로 적용 가능합니다:

```
Unified Intermediate Representation:

Element {
    type:      RECTANGLE | POLYGON | PATH | PLACEMENT | TEXT | ...
    geometry:  { layer, datatype, points/width/height/... }
    transform: { mag, angle, flip }
    props:     [{attr, value}, ...]
    positions: [ {x,y}, ... ]   // 반복 위치 (Repetition으로 인코딩 가능)
    repetition: ?               // Matrix/VaryingX/등
}
```

### 8.2 GDSII→Unified→OASIS 변환 시 손실

| 정보 | GDSII | 통합 구조 | OASIS | 손실? |
|---|---|---|---|---|
| 타임스탬프 | structure마다 | cell metadata | 없음 | **손실** |
| NODE | element | element type | 없음 | **손실** |
| BOX | element | → RECTANGLE | RECTANGLE | 타입 정보 손실 |
| TEXT transform | STRANS | transform | 없음 | special cell 우회 |
| GDSII 전용 property | PROPVALUE | property | `S_GDS_PROPERTY` | 보존 |
| ELFLAGS | per-element | options | 없음 | **손실** |
| PLEX | per-element | options | 없음 | **손실** |

### 8.3 통합 인터페이스 제안

```cpp
class UnifiedBuilder {
    // File
    virtual void beginFile(const char* version, double unit,
                           const FileOptions& options);
    virtual void endFile();

    // Cells
    virtual void beginCell(const char* name,
                           const CellOptions& options);
    virtual void endCell();

    // Elements — 각 element는 absolute position list + optional repetition
    virtual void addPlacement(const char* cellName,
                              const std::vector<Point>& positions,
                              const Transform& xform,
                              const Repetition* rep);

    virtual void addBoundary(int layer, int datatype,
                             const PointList& points,  // absolute coords
                             const std::vector<Point>& positions,
                             const Repetition* rep);

    virtual void addPath(int layer, int datatype,
                         const PointList& points,
                         int halfwidth, int startExtn, int endExtn,
                         const std::vector<Point>& positions,
                         const Repetition* rep);

    virtual void addText(int layer, int texttype,
                         const char* text,
                         const std::vector<Point>& positions,
                         const Transform& xform);

    virtual void addRectangle(int layer, int datatype,
                              int width, int height,
                              const std::vector<Point>& positions,
                              const Repetition* rep);

    virtual void addTrapezoid(int layer, int datatype,
                              const Trapezoid& trap,
                              const std::vector<Point>& positions,
                              const Repetition* rep);

    virtual void addCircle(int layer, int datatype,
                           int radius,
                           const std::vector<Point>& positions,
                           const Repetition* rep);

    virtual void addNode(int layer, int nodetype,
                         const PointList& points);

    virtual void addProperty(int attr, const char* value);

    virtual void endElement();
};
```

**핵심 설계 원칙:**
1. **Absolute positions + delta conversion은 각 포맷의 파서/라이터가 담당** (통합 구조는 항상 절대좌표)
2. **Repetition은 writer가 선택적 최적화** (positions.size() > N이면 자동 변환)
3. **GDSII 전용/OASIS 전용 필드는 optional metadata**로 보존
4. **Name table 참조는 통합 구조가 문자열로 저장** (포맷별 인코딩은 writer에서 처리)
