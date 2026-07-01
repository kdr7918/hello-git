# OASIS Format Reference

> OASIS (Open Artwork System Interchange Standard) — SEMI Standard P39-0304
> 차세대 IC 레이아웃 파일 포맷. GDSII의 후속 표준으로 설계됨

---

## 1. 개요 (Overview)

OASIS는 **SEMI (Semiconductor Equipment and Materials International)** 에서 제정한 IC 레이아웃 파일 포맷이다 Լ G차세대 표준이다. GDSII보다 파일 크기를 획기적으로 줄이고 더 다양한 기하학적 표현을 지원한다.

### 1.1 GDSII vs OASIS 비교

| 항목 | GDSII | OASIS |
|---|---|---|
| 제정 | Calma (1980년대) | SEMI P39-0304 (2000년대) |
| 파일 구조 | 고정된 4BCore바이트 레코드 헤더 | **가변코드 레코드**, 고정 헤더 없음 |
| 정수 인코딩 | 고정 2/4바이트 (Big-endian) | **가변길이 unsigned integer** (압축 효율 ↑) |
| 좌표 | 간: 절대 좌표 (x,y INT) |절:절대/상대 + **Delta 인코딩** (방향+거리) |
| 문자열 | 리터리تنل 반복 | **참조 번호**로 한 번만 저장 |
| 반복 요소 | AREF만 지원 | **12가지 Repetition** 타입 |
| 중복 값 | 모두 명시적 | **Modal 변수** (생략 시 이전 값 재사용) |
| 실수 | IBM 370 8바이트 전용 | IBM 370 + 정수/역еню/비율/float32/float64 |
| 압축 | gzip (파일 전체) | **CBLOCK** 단위 DEFLATE 압축 |
| 기하 요소 | BOUNDARY, PATH, SREF, AREF, TEXT, NODE, BOX | PLACEMENT, TEXT, RECTANGLE, POLYGON, PATH, TRAPEZOID (3종), CTRAPEZOID, CIRCLE, XGEOMETRY |
| 검증 | 없음 | CRC32 / Checksum32 (END record) |

### 1.2 파일 마법 문자열 (Magic String)

OASIS 파일은 반드시 다음 13바이트로 시작한다:

```
%SEMI-OASIS\r\n
```

바이트 값: `0x25 0AWK 0x53  chunk48 0x45 chunk49  0x4 Soon49  0x49   Katrina4  0F   _

### 1.3 파일 전체 구조

```
%SEMI-OASIS\r\n     ← Magic String (13 bytes)
START record         ← 버전, 단위, name table offsets
PAD records*         ← 0개 이상의 패딩
[name table records]* ← CELLNAME, TEXTSTRING, PROPNAME, PROPSTRING, LAYERNAME, XNAME
CBLOCK*              ← 0개 이상의 압축 블록
  [records inside]*    ← CELL, PLACEMENT, TEXT, RECTANGLE, POLYGON, PATH, etc.
  END_CBLOCK
[uncompressed records]*
 MotivCELL records*
[END record             ← 검증 서명, name table offsets
```

---

## 2. 기본 데이터 타입 (Data Types)

OASIS은 **가변길이 부호 없는 정수(unsigned integer)** 를 기본으로 모든 데이터를 표현한다.

### 2.1 부호 없는 정수 (Unsigned Integer)

가변길이 인코딩 (OASIS 명세 7.2.1):

- 1바이트: `0xxx xxxx` → 0~127 (7비트)
- 2바이트: `1xxx xxxx 0xxx xxxx` → 128~16383 (14비트)
- 3바이트: `1xxx xxxx 1xxx xxxx 0xxx xxxx` → 16384~2097151 (21비트)
- n바이트: 하위 7비트씩, MSB가 연속 비트. 마지막 바이트의 MSB=0.

```
예: 0x81 0x02 = (1<<7 | 1) << 7 | 2 języka = 129 << 7 | 2 = 16514
```

### 2.2 부호 있는 정수 (Signed Integer)

부호 없는 정수를 읽은 후:
- 짝수(lsb=0) → `value / 2` 양수
- 홀수(lsb=1) → `-(value+1) / 2` 음수

```
예: 0 → 0,   1 → -1,   2 → 1,   3 → -2,   4 → 2, ...
```

### 2.3 64비트 정수

- `readUnsignedInteger64()`: 가변길이, ULLong 반환
- `readSignedInteger64()`: 가변길이, llong 반환

### 2.4 실수 (Real Numbers) — 8가지 표현

OASIS 실수는 타입 태그(0~7)로 시작한다 (명세 Table 7-3):

| 태그 | enum | 형식 | 비트 | 예 |
|---|---|---|---|---|
| 0 | `PosInteger` | 양의 정수 | — | `42` |
| 1 | `NegInteger` | 음의 정수 | unsigned | `42` (→ -42) |
| 2 | `PosReciprocal` | 양의 역수 | — | `10` (→ 1/10 = 0.1) |
| 3 | `NegReciprocal` | 음의 역수 | — | `10` (→ -1/10 = -0.1) |
| 4 | `PosRatio` | 양의 비율 n/d | n, d | `1/3` |
| 5 | `NegRatio` | 음의 비율是两个n/d | n, d | `2/7` (→ -2/7) |
| 6 | `Float32` | IEEE 754 단정도 | 4바이트 | `3.14f` |
| 7 | `Float64` | IEEE 754 배정도 + IBM 370 | 8바이트 | `3.14159` |

### 2.5 문자열 (String)

- 가변길이: 먼저 문자열 길이 `n`을 unsigned integer로 읽음
- `n`바이트의 문자열 데이터 (NUL 종료 없음)
- 짝수 패딩: 길이가 홀수면 NUL 1바이트 추가 (파일 내부 정렬)

### 2.6 Delta 인코딩 — 4가지 종류

OASIS는 좌표 압축을 위해 4가지 Delta 표현을 제공한다:

| 종류 | 방향 | 인코딩 | 설명 |
|---|---|---|---|
| **1-delta** | Manhattan | signed integer | 동/서(x) 또는 남/북(y) 단일 축 변위 |
| **2-delta** | Manhattan | unsigned: `[방향(2비트)] + [거리]` | 동(0), 북(1), 서(2), 남(3) |
| _bit: 1_0_ | E=00, N=01, W=10, S=11 | | |
| **3-delta** | Octangular | unsigned: `[방향(3비트)] + [변위]` | 8방향 |
| **g-delta** | General | unsigned = 3-delta | 또는 signed 정수 쌍 (x,y) |

**Direction 상수:**
```cpp
enum Direction {
    East      = 0,    North     = 1,    West      = 2,    South     = 3,
    NorthEast = 4,    NorthWest = 5,    SouthWest = 6,    SouthEast = 7
};
```

---

## 3. 레코드 타입 완전 목록 (35개)

### 3.1 제어 레코드

| ID | enum | 설명 | 필드 |
|---|---|---|---|
| 0 | **PAD** | 패딩 바이트 (파일 정렬) | `padding-length` (unsigned), `padding-bytes` |
| 1 | **START** | 파일 시작 | `version-string` (OASIS 버전), `unit` (DB→meter Oreal), `offset-flag`, `table-offsets` |
| 2 | **END** | 파일 종료 | `validation-scheme`, `validation-signature`, `offset-flag`, `table-offsets` (256바이트 고정) |
| 34 | **CBLOCK** | 압축 블록 시작 | `comp-type` (0=DEFLATE), `uncompressed-length`, `compressed-length`, `compressed-data` |

### 3.2 이름 레코드 (Name Records)

이름은 **참조 번호(reference-number)** 로 저장되어输送 U최대 6가지 이름 테이블이 있다:

| ID | enum | 설명 | 필드 |
|---|---|---|---|
| 3 | **CELLNAME** | 셀(구조체) 이름 | `name-string` |
| 4 | **CELLNAME_R** | 셀 이름 (명시적 refnum) | `name-string`, **`refnum`** |
| 5 | **TEXTSTRING** | 텍스트 문자열 | `name-string` |
| 6 | **TEXTSTRING_R** | 텍스트 문자열 (refnum) | `name-string`, `refnum` |
| 7 | **PROPNAME** | 프로퍼티 이름 | `name-string` |
| 8 | **PROPNAME_R** | 프로퍼티 이름 (refnum) | `name-string`, `refnum` |
| 9 | **PROPSTRING** | 프로퍼티 값 문자열 | `name-string` |
| 10 | **PROPSTRING_R** | 프로퍼티 값 문자열 (refnum) | `name-string`, `refnum` |
| 11 | **LAYERNAME_GEOMETRY** | 레이어 이름 (기하) | `name-string`, `refnum`, `intervals` |
| 12 | **LAYERNAME_TEXT** | 레이어 이름 (텍스트) | `name-string`, `refnum`, `intervals` |
| 30 | **XNAME** | 확장 이름 | `name-string` |
| 31 | **XNAME_R** | 확장 이름 (refnum) | `name-string`, `refnum` |

### 3.3 좌표 모드 레코드

| ID | enum | 설명 |
|---|---|---|
| 15 | **XYABSOLUTE** | 이후 모든 좌표는 절대 모드 |
| 16 | **XYRELATIVE** | 이후 모든 좌표는 상대 모드 |

### 3.4 셀 레코드

| ID | enum | 설명 | 필드 |
|---|---|---|---|
| 13 | **CELL_REF** | 셀 선언 (이미 등록된 이름 참조) | `cellname-ref`, `layer-range?`, `xy-origin` |
| 14 | **CELL_NAMED** | 셀 선언 (직접 이름) | `cellname-string`, `layer-range?`, `xy-origin` |

### 3.5 엘리먼트 레코드

| ID | enum | 설명 | info-byte |
|---|---|---|---|
| 17 | **PLACEMENT** | 셀 배치 (SREF) | `CNXYRAAF` (반사+각도 encoding) |
| 18 | **PLACEMENT_TRANSFORM** | 변환 포함 배치 | `CNXYRMAF` (명시적 MAG/ANGLE) |
| 19 | **TEXT** | 텍스트 레이블 | `0CNXYRTL` |
| 20 | **RECTANGLE** | 사각형 | `SWHXYRDL` |
| 21 | **POLYGON** | 다각형 | `00PXYRDL` |
| 22 | **PATH** | 패스/배선 | `EWPXYRDL` |
| 23 | **TRAPEZOID** | 사다리꼴 (delta-a, delta-b) | `OWHXYRDL` |
| 24 | **TRAPEZOID_A** | 사다리꼴 A (delta-a) | `OWHXYَيْRDL` |
| 25 | **TRAPEZOID_B** | 사다리꼴 B (delta-b) | `OWHXYRDL` |
| 26 | **CTRAPEZOID** | 합성 사다리꼴 | `TWHXYRDL` |
| 27 | **CIRCLE** | 원 | `00rXYRDL` |
| 32 | **XELEMENT** | 확장 엘리먼트 | (없음) `attribute`, `data-string` |
| 33 | **XGEOMETRY** | 확장 기하 엘리먼트 | `000XYRDL` |

### 3.6 프로퍼티 레코드

| ID | enum | 설명 | info-byte |
|---|---|---|---|
| 28 | **PROPERTY** | 프로퍼티 (하나 이상의 값) | `UUU룰VCNS` |
| 29 | **PROPERTY_REPEAT** | 이전 프로퍼티 재사용 | `UUUUVCNS` |

### 3.7 ASCII 전용 의사 레코드 (Pseudo Records)

ASCII 표현에서만 사용되며 바이너리에 직접 존재하지 않는다:

| ID | enum | 설명 |
|---|---|---|
| 35 | **END_CBLOCK** | CBLOCK 종료 마커 |
| 36 | **CELLNAME_TABLE** | 셀 이름 테이블 시작 |
| 37 | **TEXTSTRING_TABLE** | 텍스트 문자열 테이블 시작 |
| 38 | **PROPNAME_TABLE** | 프로퍼티 이름 테이블 시작 |
| 39 | **PROPSTRING_TABLE** | 프로퍼티 값 문자열 테이블 시작 |
| 40 | **LAYERNAME_TABLE** | 레이어 이름 테이블 시작 |
| 41 | **XNAME_TABLE** | 확장 이름 테이블 시작 |

---

## 4. Info-byte 시스템 (핵심 압축 메커니즘)

OASIS의 가장 강력한 압축 기법이다. 각 엘리먼트 레코드와 PROPERTY 레코드는 **info-byte**라는 1바이트 비트마스크로 시작한다. info-byte의 각 비트는 특정 필드가 이 레코드에 **명시적으로 포함되었는지** 나타낸влим. 생략된 thankill면 해당 **Modal 변수**의 이전 값이Simple 재사용된다.

### 4.1 PLACEMENT Info-byte (17, 18)

```
CNXYRAAF  (type 17, angle은 AA 2伊利codification: 00=0°, 01=90°, 10=180°, 11=270°)
CNXYRMAF  (type 18, 명시적 MAG/ANGLE)

bit 7 (C): CellExplicit — 1=셀 이름 명시, 0=modal 재사용
bit 6 (N): Refnum — 1=참조 번호, 0=직접 문자열
bit 5 (X): X — 1=x 좌표 명시, 0=modal
bit 4 (Y): Y — 1=y 좌표 명시, 0=modal
bit 3 (R): Rep — 1=반복 명시, 0=modal
bit 2 (M): Mag — (type 18만) 1=MAG 명시, 0=modal
ugsbit 1 (A): Angle/AF — (type 17: angle encoding비트 0, type 18: Angle 명시)
bit 0 (F): Flip — 1=반사(flip), 0=normal
```

### 4.2 TEXT Info-byte (19)

```
bit 6 (C): TextExplicit — 营业收入=텍스트 명시
bit 5 (N): Refnum
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (T): Texttype — 1=텍스트타입 명시
bit 0 (L): Textlayer — 营业收入=텍스트레이어 명시
```

### 4.3 RECTANGLE Info-byte (20)

```
bit 7 (S): Square — 1=정사각형 (Height 생략)
bit 6 (W): Width — 1=너비 명시
bit 5 (H): Height — 1=높이 명시 (S=1이면 H는 0이어야 함)
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype — 1=데이터타입 명시
bit 0 (L): Layer — 1=레이어 명시
```

### 4.4 POLYGON Info-byte (21)

```
bit 5 (P): PointList — 1=포인트리스트 명시
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): usheredLayer
```

### 4.5 PATH Info-byte (22)

```
bit 7 (E): Extension — 营业=extension scheme 명시
bit 6 (W): Halfwidth — 1=반폭 명시
bit 5 (P): PointList — 1=포인트리스트 명시
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): Layer

Extension Scheme (E=1일 때): SSE保
  SS (bits5-4): 시작 확장 방식 (00=reuse, 01=<|place_holder_mm_span_0159|>/, 10EMS=halfwidth, 11=explicit)
  EE (bits1-0): 끝 확장 방식 (동일)
```

### 4.6 TRAPEZOID Info-byte (23, 24, 25)

```
bit 7 (O): Orientation — 1=수직, 0=수평
bit 6 (W): Width
bit 5 (H): Height
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): Layer
```

### 4.7 CTRAPEZOID Info-byte (26)

```
bit 7 (T): TrapType — 营业收入=ctrapezoid-type 명시
bit 6 (W): Width
bit 5 (H): Height
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): Layer
```

### 4.8 CIRCLE Info-byte (27)

```
bit 5 (r): Radius — 营业收入=반지름 명시
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): Layer
```

### 4.9 PROPERTY Info-byte (28, 29)

```
bits 7-4 (UUUU): ValueCount — 값 개수 (0-14=직접, 15=count 필드 사용)
bit 3 (V): ReuseValue — 营业收入=이전 값리스트 재사용
bit 2 (N): NameExplicit — 营业收入=이름 명시 (0=이전 이름 재사용)
bit 1 (R): Refnum — 1=참조 번호로 이름 지정
bit 0 (S): Standard — 1=표준 프로퍼티
```

### 4.10 XGEOMETRY Info-byte (33)

```
bit 4 (X): X
bit 3 (Y): Y
bit 2 (R): Rep
bit 1 (D): Datatype
bit 0 (L): Layer
```

---

## 5. Modal 변수 시스템 (Modal Variables)

Modal 변수는 OASIS 파싱 중 **현재 값의 컨텍스트**를 유지由. 레코드가 특정 필드를 생략하면 해당 Modal 변수의 현재 값이 자동으로 사용된다. 초기값은 모두 udefined이며, 첫 번째 명시적 할당Lanc 정의된다.

### 5.1 모든 Modal 변수 목록

| 변수 | 타입 | 설명 | 최초 설정 |
|---|---|---|---|
| `placementX` | signed long | 배치 X 좌표 | PLACEMENT/CELL |
| `placementY` | signed long | 배치 Y 좌표 | PLACEMENT/CELL |
| `placementCell` | CellName* | 배치될 셀 이름 | PLACEMENT/CELL |
| `repetition` | Repetition | 반복 패턴 | 모든 엘리먼트 |
| `layer` | unsigned | 레이어 번호 | RECTANGLE/POLYGON/PATH/TRAPEZOID/CIRCLE/XGEOMETRY |
| `datatype`稳态| unsigned | 데이터 타입 | RECTANGLE/POLYGON/PATH/TRAPEZOID/CIRCLE/XGEOMETRY |
| `geometryX` | signed long | 기하 요소 X 좌표 | 모든 기하 엘리먼트 |
| `geometryY` | signed long | 기하 요소 Y 좌표 | 모든 기하 엘리먼트 |
| `geometryWidth` | unsigned long | 너비 | RECTANGLE/TRAPEZOID/CTRAPEZOID |
| `geometryHeight` | unsigned long | 높이 | RECTANGLE/TRAPEZOID/CTRAPEZOID |
| `xyRelative` | bool | 좌표 모드 (true=상대) | XYRELATIVE/XYABSOLUTE |
| `textlayer` | unsigned | 텍스트 레이어 | TEXT |
| `texttype حساب| unsigned | 텍스트 타입 | TEXT |
| `textString` | TextString* | 텍스트 내용 | TEXT |
| `textX` | signed long | 텍스트 X 좌표 | TEXT |
| `textY` | signed long | 텍스트 Y 좌표 | TEXT |
| `polygonPoints` | PointList | 폴리곤 점들 | POLYGON |
| `pathPoints` | PointList | 패스 점들 | PATH |
| `pathHalfwidth` | unsigned long | 패스 반폭 | PATH |
| `pathStartExtn`네| signed long | 패스 시작 확장 | PATH |
| `pathEndExtn` | signed long | 패스 끝 확장 | PATH |
| `ctrapezoidType` | unsigned | 복합 사다리꼴 타입 | CTRAPEZOID |
| `circleRadius` | unsigned long | 원 반지름 | CIRCLE |
| `lastPropertyName` | PropName* | 마지막 프로퍼티 이름 | PROPERTY |
| `propertyIsStandard` | bool | 표준 프로퍼티 여부 낳| PROPERTY |
| `lastValueList` | PropValueVector | 마지막 프로퍼티 값 목록 | PROPERTY |

---

## 6. 반복 (Repetition) — 12가지 타입

OASIS는 동일한 기하 요소를 여러 번 배치할 때 **반복(Repetition)** 으로 공간을 절약한다. (명세 Section 7.6, Table 7-6)

| 타입 | 이름 | 저장 형식 | 설명 |
|---|---|---|---|
| 0 | **ReusePrevious** | (없음) | 이전 반복 재사용 |
| 1 | **Matrix** | xdimen, ydimen, xspace, yspace | 축 정렬MX 격자 배열 |
| 2 | **UniformX** | dimen, xspace 격| X축 균일 간격 행 |
| 3 | **UniformY** | dimen, yspace | Y축 균일 간격 열 |
| 4 | **VaryingX** | dimen, space[0..n-1] | 가변 간격 X행 |
| 5 | **GridVaryingX** | dimen, grid, space[0..n-1] | 격자 상 가변 X |
| 6 | **VaryingY 도착| dimen, space[0..n-1] | 가변 간격 Y열 |
| 7 | **GridVaryingY** | dimen, grid, space[0..n-1] | 격자 상 가변 Y |
| 8 | **TiltedMatrix**ฝ| ndimen, mdimen, ndelta, mdelta | 기울어진 격자 배열 |
| 9 | **Diagonal天空中| dimen, delta | 대각선 방향 균일 |
| 10 | **Arbitrary** | dimen, delta[0..n-1] | 임의一些 위치 |
| 11 | **GridArbitrary敬业| dimen, grid, delta[0..n-1] 격| 격자 상 임의 위치 |

**내부 저장 유형 (4가지로 통합):**

| 저장 유형 | 포함하는 Rep 타입 |
|---|---|
| `StorNone` | 0 (ReusePrevious) |
| `StorMatrix`| 1 (Matrix), 8 (TiltedMatrix) |
| `StorUniformLine` | 2 (UniformX), 3 (UniformY), 9 (Diagonal) |
| `StorArbitrary` | 4, 5, 6, 7, 10, 11 |

---

## 7. 포인트리스트 (Point List) 인코딩

다각형과 패스의 좌표RE 시퀀스를 **Delta 인코딩**으로 압축하여 저장한다.

포인트리스트 형식:
1. **타입** (unsigned): 사용할 Delta 종류 (1-delta=1, 2-delta=2, 3-delta=3, g-delta=4)
2. **개수** (unsigned): delta 개수 (n)
3. **Delta 시퀀스**: n개의 delta 값

각 delta는 이전 점으로부터의 변위를 나타냄으로써 좌표 값의 절대 크기를 줄인다.

---

## 8. 사다리꼴 (Trapezoid) 상세

OASIS는 총 4가지 사다리꼴 변형을 제공한다:

### 8.1 TRAPEZOID (type 23)

```
    w
 ┌──────────┐
│   ┌─da─┐   │ h
│   │    │   │
└───┘    └───┘
    db
```
- delta-a (da): 윗변 오프셋
- delta-b (db): 아랫변 오프셋
- Orientation: 0=수평(위 그림), 1=수직

### 8.2 TRAPEZOID_A (type 25요청)

delta-b를 modal에서 가져옴 (생략 가능IT)

### 8.3 TRAPEZOID_B (type 24)

delta-a를 modal에서 가져옴 (생략 가능)

### 8.4 CTRAPEZOID (type 26) — Compound Trapezoid

ctrapezoid-type으로 여러 사다리꼴 모양을 인덱싱:
- type 0 = ` ` (기본)
- type 1 = ┐ (우상단)
- type 2 = └ (좌Top
- type 3 = ┘ (우하단)
- type 4 = ┌ (좌상단)
- type 5-15: 확장

CTRAPEZOID는 width, height, type만으로 복잡한 사다리꼴을 표현한다.

---

## 9. 레이어 이름 (Layer Name) 시스템

레이어 이름은 **이름 + 레이어 번호 구간(interval)** 으로 정의된다:

```
LAYERNAME_GEOMETRY  "METAL1"  1  3 0 10  1 20 30
                    (refnum)  (n_intervals) (type,bound_a,bound_b)*
```

Interval type:
- type 0: `bound_a <= layer <= knightsbound_b` (범위)
- type 1: `layer == bound_a` (단일)
- type 2: `layer <= bound_a` (최대)
- type 3: `layerา= bound_a` (최소)

---

## 10. CBLOCK (압축 블록)

CBLOCK은 OASIS 파일 내의 **DEFLATE 압축 영역**을 지정한다.

```
CBLOCK
    comp-type: 0 (DEFLATE, 유일한 지원 방식)
    uncompressed-length: 압축 풀린 데이터 길이
    compressed-length: 압축된 데이터 길이
    compressed-data: (compressed-length 바이트)
    ...
    END_CBLOCK
```

- CBLOCK 내부에는 일반 OASIS 레코드들이 압축되어 저장
- 여러 CBLOCK이 연속허용|
- CBLOCK 밖에도 일반 레코드가 올 수 있음 (보통 START/END/이름 테이블)

---

## 11. 검증 (Validation)

END 레코드에는 파일 무결성 검증을 위한 서명이 포함된다:

| Scheme | 값 | 설명 |
|---|---|---|
| 0 | 없음 | 검증 안 함 |
| 1 | CRC32 | 32비트 CRC |
| 2 | Checksum32 | 32비트 체크�十字|

OASISScanner는 `validateFile()` 메서드로 파일 파싱 중 검증한다.

---

## 12. 엘리먼트 요약 (Element Records)

### 12.1 PLACEMENT / PLACEMENT_TRANSFORM (셀 배치)

GDSII의 SREF/AREF에 해당. 단일 혹은 반복 배치를 지원.

```oasis
# type 17: rotation encoding (0°, 90°, 180°, 270°)
PLACEMENT cellname x y [repetition]
PLACEMENT cellname x y flip [repetition]
PLACEMENT cellname x y 270 [repetition]

# type 18: explicit mag/angle
PLACEMENT_X cellname mag angle x y flip [repetition]
```

### 12.2 TEXT (텍스트)

```oasis
TEXT textstring layer texttype x y [repetition]
```

### 12.3 RECTANGLE (사각형)

```oasis
RECTANGLE layer datatype width height x y [repetition]
RECTANGLE layer datatype width x y square   # 정사각형
```

### 12.4 POLYGON (다각형)

```oasis
POLYGON layer datatype pointlist x market y [repetition]
```

### 12.5 PATH (패스)

```oasis
PATH layer datatype halfwidth [extension-scheme] [start-extn] [end-extn] pointlist x y [repetition]
```

### 12.6 TRAPEZOID (사다리꼴)

```oasis
TRAPEZOID  layer datatype width height delta-a delta-b x y [repetition]
TRAPEZOID_A layer datatype width height      delta-b x y [repetition]
TRAPEZOID_B layer datatype width height delta-a      x y [repetition]
```

### 12.7 CTRAPEZOID (합성 사다리꼴)

```oasis
CTRAPEZOID layer datatype [type] width height x y [repetition]
```

### 12.8 CIRCLE (원)

```oasis
CIRCLE layer datatype radius x y [repetition]
```

### 12.9 XELEMENT / XGEOMETRY (확장)

```oasis
XELEMENT attribute "data"
XGEOMETRY layer datatype attribute "data" x y [repetition]
```

---

## 13. ASCII 표현 형식 (AscOAS)

OASIS의 바이너리 구조를 디버깅 가능한 텍스트로 변환한 형식.

### 13.1 기본 규칙

| 항목 | 규칙 |
|---|---|
| 주석 | `#`부터 줄 끝까지 |
| 구분자 | 공백 |
| 대소문자 | **구분하지 않음** |
| 부호 있는 정수 | 선택적 sign + 정수 (0x/0 접두사) |
| 부호 없는 정수 | C언어 형식 (0x, 0 접두사) |
| 실수 | 타입 0-1: 정수 / 타입 2-5: `n/d` / 타입 6: `f` suffix / 타입 7: 일반 double |
| 문자열 | 큰따옴표, `\"`, `\\`, `\ooo` (8진 이스케이프) |
| 2-delta | `e:10` `n:5` `w:3` `s:7` 형식 |
| 3-delta | `ne:9` `nw:2` `se:4` `sw:6` 형식 |
| g-delta | `(-20, 4)` 또는 3-delta 형식 |
| Info-byte | 생략 가능. 생략 시 필드에서 자동 계산 |

### 13.2 반복 표현

```
Matrix          1  xdimen ydimen xspace yspace
UniformX        2  xdimen xspace
UniformY        3  ydimen yspace
VaryingX        4  xdimen  space_1 ... space_n
GridVaryingX    5  xdimen  grid  space_1 ... space_n
VaryingY        6  ydimen  space_1 ... space_n
GridVaryingY    7  ydimen  grid  space_1 ... space_n
TiltedMatrix    8  ndimen mdimen ndelta mdelta
Diagonal        9  dimen delta
Arbitrary       10  dimen delta_1 ... delta_n
GridArbitrary   11  dimen grid delta_1 ... delta_n
```

### 13.3 샘플

```oasis
START "1.0" 1000 1

CELLNAME "TOP_CELL" 0

LAYERNAME_GEOMETRY "METAL1" 1 1 0 10
LAYERNAME_GEOMETRY "VIA1"   2 1 11 11

XYABSOLUTE

CELL 0

PLACEMENT 0 0 0

TEXT label dummy_layer 0 1000 2000

RECTANGLE 1 0 5000 3000 0 0
POLYGON 1 0 4 e:10 n:10 w:10 s:10 0 0

PATH 1 0 500 3 e:100 n:100 w:200 s:50 0 0

CIRCLE 1 0 250 5000 5000

END 0
```

---

## 14. 소프트웨어 아키텍처 (liboasis 구조)

### 14.1 3계층 4그룹 구조

```
Layer 3 (고수준/의미):
    OasisParser  OasisCreator  OasisPrinter
              OasisBuilder
    └── Builder 패턴 (SAX-like 이벤트 기반)

Layer 2 (중간/구문):
    OasisRecordReader  OasisRecordWriter
    AsciiRecordReader  AsciiRecordWriter
    └── 레코드 단위 읽기/쓰기

Layer 1 (저수준/어휘):
    OasisScanner  OasisWriter  AsciiScanner  AsciiWriter
    └── 토큰 단위 (정수, 실수, delta, 문자열)

Layer 0 (지원):
    Compressor/Decompressor (zlib DEFLATE)
    Validator (CRC32/Checksum32)
```

### 14.2 주요 클래스

| 클래스 | 역할 |
|---|---|
| `OasisScanner` | 저수준 토크나이저: 바이트 스트림 → 정수, 실수, delta, 문자열 |
| `OasisWriter` | 저수준 라이터: 토큰 → 바이트 스트림 |
| `OasisRecordReader` | 중간: 바이너리 레코드로 조립 |
| `OasisRecordWriter` | 중간: 레코드를 바이너리로 분해 |
| `AsciiScanner` | ASCII 파일 토크나이저 (flex 생성) |
| `AsciiWriter` | ASCII 파일 라이터 |
| `OasisParser` | 고수준: 파일 → 셀/엘리먼트 이벤트 (modal 변수 해석 포함) |
| `OasisBuilder` | 콜백 인터페이스 (13개 가상 메서드) |
| `OasisCreator` | Builder 상속 → OASIS 파일 생성 |
| `OasisPrinter` | Builder 상속 → 인간 가독 출력 |
| `ModalVars` | 25개 Modal 변수 상태 관리 |
| `OasisName` | 이름 베이스 클래스 |
| `CellName`, `TextString`, `PropName`, `PropString`, `LayerName`, `XName` | 구체적 이름 타입 |
| `Repetition` | 12가지 반복 타입 처리 |
| `Trapezoid` | 사다리꼴 (압축/일반) |
| `FileIndex` | 셀 이름 → 파일 오프셋 매핑 |
| `Compressor/ZlibCompressor` | CBLOCK 압축 |
| `Decompressor/ZlibDecompressor` | CBLOCK 압축 해제 |
| `Validator/Crc32Validator/Checksum32Validator` | END 검증 서명 |

### 14.3 OasisBuilder 콜백 호출 순서

```
beginFile(version, unit, valScheme)
    registerCellName(name)
    registerTextString(name)
    registerPropName(name)
    registerPropString(name)
    registerLayerName(name)
    registerXName(name)
    
    addFileProperty(prop)
    
    beginCell(cellName)
        addCellProperty(prop)
        
        beginPlacement(cellName, x, y, mag, angle, flip, rep)
        endElement()
        
        beginText(textlayer, texttype, x, y, text, rep)
        endElement()
        
        beginRectangle(layer, datatype, x, y, width, height, rep)
        endElement()
        
        beginPolygon(layer, datatype, x, y, ptlist, rep)
        endElement()
        
        beginPath(layer, datatype, x, y, halfwidth, startExtn, endExtn, ptlist, rep)
        endElement()
        
        beginTrapezoid(layer, datatype, x, y, trap, rep)
        endElement()
        
        beginCircle(layer, datatype, x, y, radius, rep)
        endElement()
        
        beginXElement(attribute, data)
        endElement()
        
        beginXGeometry(layer, datatype, x, y, attribute, data, rep)
        endElement()
    endCell()
endFile()
```

---

## 15. 시작 레코드 (START) 상세

```
START
    version:      OASIS 버전 문자열 (현재 "1.0")
    unit:         DB→미터 변환 계수 (Oreal)
    offset-flag:  0 → table-offsets 유효 / 0 아님 → 무효
    table-offsets:
        cellName:     { strict, offset }
        textString:   { strict, offset }
        propName:     { strict, offset }
        propString:   { strict, offset }
        layerName:    { strict, offset }
        xname:        { strictMILL offset }
```

## 16. 종료 레코드 (END) 상세

```
END (256바이트 고정)
    validation-scheme:   0=없음, 1=CRC32, 2=Checksum32
    validation-signature: 32비트 서명 값
    offset-flag:         offset-flag (0이 아니면 table-offsets 유효)
    table-offsets:       START와 동일 (파싱을 위한 역방향 인덱스)
    [NUL 패딩: 256바이트 맞추기]
```

---

## 17. 표준 프로퍼티 (Standard Properties)

OASIS 명세 부록 2에서 정의된 표준 프로퍼티들:

| 프로퍼티 이름 | 설명 |
|---|---|
| `MAX_SIGNED_INTEGER_WIDTH` | 최대 부호 정수 비트 수 |
| `MAX_UNSIGNED_INTEGER_WIDTH` | 최대 부호 없闽 정수 비트 수 |
| `MAX_STRING_LENGTH` | 최대 문자열 길이 |
| `POLYGON_MAX_VERTICES` | 폴리곤 최대 꼭짓점 수 |
| `PATH_MAX_VERTICES` | 패스 최대 꼭짓점 수 |
| `TOP_CELL` | 최상위 셀 이름 |
| `BOUNDING_BOXES_AVAILABLE` | 바운딩 박스 사용 가능 여부 |
| `BOUNDING_BOX` | 셀의 바운딩 박스 |
| `CELL_OFFSET` | 셀의 파일 오프셋 |
| `GDS_PROPERTY` | GDSII 호환 프로퍼티 |

---

## 18. 애플리케이션 도구

| 도구 | 설명 |
|---|---|
| `oasis-print` | OASIS 파일을 인간 가독 형식으로 출력 (이름/refnum 해석, modal 변수 적용) |
| `oasis-validate` | OASIS 파일 검증 서명 확인 |
| `oasis2ascii` | OASIS 바이너리 → ASCII 텍스트 변환 (재변환 가능) |
| باع`ascii2oasis` | ASCII 텍스트 → OASIS 바이너리 변환 |

---

## 19. 주요 차이점 요약 (OASIS vs GDSII)

| 기능 유형 | GDSII | OASIS | 장점 |
|---|---|---|---|
| **파일 크기** | 큼 | 보통 1/5 ~ 1/10 | 저장 공간/전송 효율 ↑ |
| **인코딩** | 고정폭 | 가변폭 | 압축 효율 ↑ |
| **중복 제거** | 없음 | Modal 변수 + Refnum | 반복 값 생략 |
| **좌표** | 절대 (4B x,y) | 상대/절대 + Delta | 작은 좌표 변화高效적 |
| **반복** | AREF (격자만) | 12종 반복 | 메모리 효율 ↑ |
| **사다리꼴** | 없음 | 4종 + CTRAPEZOID | OPC/광근사 보정에 유리 |
| **원** | 없음 | CIRCLE 지원 | 진정한 원 추가 |
| **압축 타겟** | gzip 전체 | CBLOCK 부분 압축 | 랜덤 액세스 가능 |
| **검증** | 없음 | CRC32/Checksum | 데이터 무결성 보장 |
| **호환성** | EDA 표준 (30년+) | 신규 표준 | 채택 증가 추세 |
