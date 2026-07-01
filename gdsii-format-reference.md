# GDSII Stream Format Reference

> GDSII Stream Format — IC 레이아웃 파일 포맷의 완전한 기술 명세
> GDSII = Graphics Design System II (구 EDA 툴 포맷, 현재 Calma GDSII로도 알려짐)
> 분석 기준: [anuvad](https://github.com/kdr7918/anuvad) 프로젝트의 libgdsii 소스코드 (SoftJin Infotech)

---

## 1. 개요 (Overview)

GDSII Stream은 **IC(집적회로) 레이아웃**을 표현하는 **바이너리 파일 포맷**이다. 계층적 구조를 가지며, 라이브러리 → 구조체(셀) → 엘리먼트 → 프로퍼티의 4단계 계층으로 구성된다.

### 1.1 포맷 특징

| 항목 | 내용 |
|---|---|
| 파일 확장자 | `.gds`, `.gdsii` |
| 바이트 순서 | **Big-endian** (네트워크 바이트 순서) |
| 기본 단위 | 가변길이 **레코드(record)** 스트림 |
| 최대 레코드 크기 | 65,534 바이트 (헤더 포함) |
| 좌표계 | 2D 정수 좌표 (데이터베이스 유닛) |
| 실수 형식 | **IBM 370 부동소수점** (base-16, 8바이트) |
| gzip 압축 | 지원 (투명하게 읽기/쓰기 가능) |

---

## 2. 레코드 구조 (Record Format)

GDSII 파일은 **연속된 가변길이 레코드**들로 구성된다. 각 레코드는 **4바이트 헤더**와 **가변길이 바디**로 이루어진다.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├───────────────────────┬──────────┬──────────┬──────────────────────┤
│     레코드 길이 (2B)    │레코드타입│ 데이터타입│    레코드 바디       │
│   (헤더 포함, 항상 짝수) │   (1B)   │   (1B)   │    (가변길이)        │
├───────────────────────┴──────────┴──────────┴──────────────────────┤
│<---------------------------- 레코드 길이 ------------------------->│
```

### 2.1 헤더 상세

| 오프셋 | 크기 | 필드 | 설명 |
|---|---|---|---|
| 0-1 | 2바이트 (uint16 BE) | **Record Length** | 레코드 전체 길이 (헤더 4바이트 + 바디). 항상 **짝수**. 최소 4, 최대 65534 |
| 2 | 1바이트 | **Record Type** | 레코드 종류 (GRT_XXX, 0~69) |
| 3 | 1바이트 | **Data Type** | 데이터 타입 (GDATA_XXX, 0~6) |

### 2.2 데이터 타입 (Data Types)

| 값 | enum | 이름 | 크기 | 설명 |
|---|---|---|---|---|
| 0 | `GDATA_NONE` | No Data | 0바이트 | 데이터 없는 레코드 (마커 역할) |
| 1 | `GDATA_BITARRAY` | Bit Array | 2바이트 | 비트 플래그 (항상 1개 값만 있음) |
| 2 | `GDATA_SHORT` | 2-Byte Integer | 2바이트 | 16비트 부호 있는 정수 (int16) |
| 3 | `GDATA_INT` | 4-Byte Integer | 4바이트 | 32비트 부호 있는 정수 (int32) |
| 4 | *(미사용)* | 4-Byte Float | 4바이트 | GDSII 명세에 정의되나 **실제로 사용 안 함** |
| 5 | `GDATA_DOUBLE` | 8-Byte Real | 8바이트 | IBM 370 부동소수점 |
| 6 | `GDATA_STRING` | String | 가변 | ASCII 문자열 (NUL 패딩, 길이는 항상 짝수) |

### 2.3 IBM 370 부동소수점 형식 (8-Byte Real 변환)

GDSII는 IEEE 754가 아닌 **IBM System/370 부동소수점** 형식을 사용한다.

```
바이트 0:  S[1]  EEEEEEE[7]
바이트 1-7: 맨티사 (7바이트, base-256)
```

- **S** (1비트): 부호 비트 (0=양수, 1=음수)
- **E** (7비트): base-16 지수, **64 bias** (실제 지수 = 저장값 - 64)
- **맨티사**: 7바이트, base-256 소수점, **hidden bit 없음** (= IEEE 754와의 핵심 차이)
- **실제 값**: `(-1)^S × significand × 16^(E-64)`
  - significand = Σ(byte[j] / 256^j) for j = 1..7
  - base-16이므로 1 nibble이 1 digit: 14 hex digits 정밀도 (≈ 56비트)

**변환 공식:**
```c
// GDSII Real → IEEE 754 Double
sign = (str[0] & 0x80) ? -1.0 : 1.0;
exponent = 4 * ((str[0] & 0x7f) - 64);   // base-2 지수
// 맨티사: 7바이트를 base-256 소수로
result = sign * ldexp(significand, exponent);

// IEEE 754 Double → GDSII Real
frexp(val, &exponent);      // base-2 분해
// 지수를 4의 배수로 조정 (base-16으로 변환)
exponent = exponent/4 + 64;  // bias 추가
// 맨티사: 반복적 256 곱셈으로 7바이트 추출
```

---

## 3. 레코드 타입 완전 목록 (70개, GRT_HEADER ~ GRT_CONTACT)

> `valid=true`: 실제 사용되는 레코드 | `valid=false`: 명세에는 있지만 미사용

### 3.1 라이브러리 헤더 레코드 (Library Header)

| # | enum | valid | 데이터 타입 | 크기 | min | max | 설명 |
|---|---|---|---|---|---|---|---|
| 0 | **HEADER** | ✓ | SHORT | 2 | 2 | 2 | GDSII 버전 번호 |
| 1 | **BGNLIB** | ✓ | SHORT | 2 | 24 | 24 | 라이브러리 시작. 타임스탬프 (6×short=12B, 수정/접근 각각 = 24B) |
| 2 | **LIBNAME** | ✓ | STRING | var | 0 | 65530 | 라이브러리 파일 이름 |
| 3 | **UNITS** | ✓ | DOUBLE | 8 | 16 | 16 | 측정 단위 (2×double: DB→user, DB→meter) |
| 4 | **ENDLIB** | ✓ | NONE | 0 | 0 | 0 | 라이브러리 종료 |
| 31 | **REFLIBS** | ✓ | STRING | 44 | 88 | 748 | 참조 라이브러리 목록 (2~17개, 각 44B 고정) |
| 32 | **FONTS** | ✓ | STRING | 44 | 176 | 176 | 폰트 파일 이름 4개 (각 44B 고정) |
| 34 | **GENERATIONS** | ✓ | SHORT | 2 | 2 | 2 | 세대 보존 개수 (기본값 3) |
| 35 | **ATTRTABLE** | ✓ | STRING | var | 0 | 65530 | 속성 테이블 파일 경로 (최대 44B) |
| 50 | **TAPENUM** | ✓ | SHORT | 2 | 2 | 2 | 테이프 볼륨 번호 |
| 51 | **TAPECODE** | ✓ | SHORT | 2 | 12 | 12 | 테이프 레이블 정보 (6×short) |
| 52 | **STRCLASS** | ✓ | BITARRAY | 2 | 2 | 2 | 구조체 클래스 (비 Cadence 툴: 반드시 0) |
| 54 | **FORMAT** | ✓ | SHORT | 2 | 2 | 2 | 파일 포맷 유형 (0=GDSII Archive, 1=GDSII Filtered, 2=EDSIII Archive, 3=EDSIII Filtered) |
| 55 | **MASK** | ✓ | STRING | var | 2 | 65530 | 필터 마스크 (FORMAT=Filtered일 때 1개 이상) |
| 56 | **ENDMASKS** | ✓ | NONE | 0 | 0 | 0 | 마스크 목록 종료 |
| 57 | **LIBDIRSIZE** | ✓ | SHORT | 2 | 2 | 2 | 라이브러리 디렉토리 크기 |
| 58 | **SRFNAME** | ✓ | STRING | var | 2 | 65530 | 규칙 파일 이름 (spacing/rules) |
| 59 | **LIBSECUR** | ✓ | SHORT | 2 | 6 | 192 | ACL 엔트리 (group, user, access × N, 최대 32 트리플렛) |

### 3.2 구조체 레코드 (Structure Records)

| # | enum | valid | 데이터 타입 | 크기 | min | max | 설명 |
|---|---|---|---|---|---|---|---|
| 5 | **BGNSTR** | ✓ | SHORT | 2 | 24 | 24 | 구조체 시작. 타임스탬프 (생성/수정 각각 12B=24B) |
| 6 | **STRNAME** | ✓ | STRING | var | 2 | 65530 | 구조체(셀) 이름. 명세상 최대 32자이지만 파서는 512자까지 허용 |
| 7 | **ENDSTR** | ✓ | NONE | 0 | 0 | 0 | 구조체 종료 |

### 3.3 엘리먼트 레코드 (Element Records)

| # | enum | valid | 데이터 타입 | 크기 | min | max | 설명 |
|---|---|---|---|---|---|---|---|
| 8 | **BOUNDARY** | ✓ | NONE | 0 | 0 | 0 | 폴리곤 경계 (다각형) |
| 9 | **PATH** | ✓ | NONE | 0 | 0 | 0 | 패스 (선/트레이스) |
| 10 | **SREF** | ✓ | NONE | 0 | 0 | 0 | 단순 셀 참조 (Structure Reference) |
| 11 | **AREF** | ✓ | NONE | 0 | 0 | 0 | 배열 셀 참조 (Array Reference) |
| 12 | **TEXT** | ✓ | NONE | 0 | 0 | 0 | 텍스트 레이블 |
| 20 | **TEXTNODE** | ✓ | NONE | 0 | 0 | 0 | 텍스트 노드 |
| 21 | **NODE** | ✓ | NONE | 0 | 0 | 0 | 노드 |
| 45 | **BOX** | ✓ | NONE | 0 | 0 | 0 | 박스 (직사각형) |

### 3.4 엘리먼트 속성 레코드

| # | enum | valid | 데이터 타입 | 크기 | min | max | 설명 |
|---|---|---|---|---|---|---|---|
| 13 | **LAYER** | ✓ | SHORT | 2 | 2 | 2 | 레이어 번호 (0~255 spec, 실제 0~32767) |
| 14 | **DATATYPE** | ✓ | SHORT | 2 | 2 | 2 | 데이터 타입 번호 (0~255) |
| 15 | **WIDTH** | ✓ | INT | 4 | 4 | 4 | 패스/텍스트 폭 (음수=절대값, MAG 영향 안 받음) |
| 16 | **XY** | ✓ | INT | 4 | 8 | 65528 | 좌표 리스트 (x,y 쌍, 각 4B) |
| 17 | **ENDEL** | ✓ | NONE | 0 | 0 | 0 | 엘리먼트 종료 |
| 18 | **SNAME** | ✓ | STRING | var | 2 | 65530 | 참조되는 구조체 이름 (SREF/AREF) |
| 19 | **COLROW** | ✓ | SHORT | 2 | 4 | 4 | 배열 열/행 개수 (AREF: columns, rows, 각 2B) |
| 22 | **TEXTTYPE** | ✓ | SHORT | 2 | 2 | 2 | 텍스트 타입 번호 |
| 23 | **PRESENTATION** | ✓ | BITARRAY | 2 | 2 | 2 | 텍스트 표시 속성 (폰트, 정렬) |
| 24 | **SPACING** | ✗ | - | - | - | - | 미사용 |
| 25 | **STRING** | ✓ | STRING | var | 0 | 65530 | 텍스트 문자열 내용 |
| 26 | **STRANS** | ✓ | BITARRAY | 2 | 2 | 2 | 변환/반사 플래그 (SREF/AREF/TEXT) |
| 27 | **MAG** | ✓ | DOUBLE | 8 | 8 | 8 | 확대/축소 비율 |
| 28 | **ANGLE** | ✓ | DOUBLE | 8 | 8 | 8 | 회전 각도 (도, 반시계 방향) |
| 33 | **PATHTYPE** | ✓ | SHORT | 2 | 2 | 2 | 패스 끝점 유형 |
| 38 | **ELFLAGS** | ✓ | BITARRAY | 2 | 2 | 2 | 엘리먼트 플래그 (external data, template) |
| 42 | **NODETYPE** | ✓ | SHORT | 2 | 2 | 2 | 노드 타입 번호 |
| 43 | **PROPATTR** | ✓ | SHORT | 2 | 2 | 2 | 프로퍼티 속성 번호 (0~127) |
| 44 | **PROPVALUE** | ✓ | STRING | var | 0 | 65530 | 프로퍼티 값 문자열 |
| 46 | **BOXTYPE** | ✓ | SHORT | 2 | 2 | 2 | 박스 타입 번호 |
| 47 | **PLEX** | ✓ | INT | 4 | 4 | 4 | PLEX 계층 그룹 ID |
| 48 | **BGNEXTN** | ✓ | INT | 4 | 4 | 4 | 패스 시작 연장 길이 |
| 49 | **ENDEXTN** | ✓ | INT | 4 | 4 | 4 | 패스 끝 연장 길이 |

### 3.5 미사용/레거시 레코드 (valid=false)

| # | enum | 설명 |
|---|---|---|
| 24 | SPACING | 미사용 |
| 29 | UINTEGER | 미사용 (부호 없는 정수) |
| 30 | USTRING | 미사용 (부호 없는 문자열) |
| 36 | STYPTABLE | 미사용 |
| 37 | STRTYPE | 미사용 |
| 39 | ELKEY | 미사용 |
| 40 | LINKTYPE | 미사용 |
| 41 | LINKKEYS | 미사용 |
| 53 | RESERVED | 예약됨 |

### 3.6 파서가 거부하는 레코드 (EBNF 문법 밖)

다음 레코드들은 GDSII 명세의 EBNF 문법에 정의되지 않아 파서가 완전히 무시한다:

20 TEXTNODE, 24 SPACING, 29 UINTEGER, 30 USTRING, 36 STYPTABLE,
37 STRTYPE, 39 ELKEY, 40 LINKTYPE, 41 LINKKEYS, 50 TAPENUM,
51 TAPECODE, 53 RESERVED, 60 BORDER, 61 SOFTFENCE, 62 HARDFENCE,
63 SOFTWIRE, 64 HARDWIRE, 65 PATHPORT, 66 NODEPORT, 67 USERCONSTRAINT,
68 SPACER_ERROR, 69 CONTACT

---

## 4. 계층 구조 (Hierarchy) — EBNF 문법

GDSII 파일은 다음과 같은 엄격한 계층 구조를 따른다:

```
<file>      ::= HEADER BGNLIB [library_options] LIBNAME [REFLIBS] [FONTS]
                [ATTRTABLE] [GENERATIONS] [FormatType] UNITS
                { <structure> }*
                ENDLIB

<library_options> ::= [LIBDIRSIZE] [SRFNAME] [LIBSECUR]

<FormatType> ::= FORMAT
               | FORMAT {MASK}+ ENDMASKS

<structure> ::= BGNSTR STRNAME [STRCLASS]
                { <element> }*
                ENDSTR

<element>   ::= <beginElem> { <property> }* ENDEL

<beginElem> ::= BOUNDARY {LAYER DATATYPE XY}
              | PATH    {LAYER DATATYPE [PATHTYPE] [WIDTH]
                         [BGNEXTN] [ENDEXTN] XY}
              | SREF    {SNAME [STRANS] XY}
              | AREF    {SNAME [STRANS] COLROW XY}
              | NODE    {LAYER NODETYPE XY}
              | BOX     {LAYER BOXTYPE XY}
              | TEXT    {LAYER TEXTTYPE [PRESENTATION] [PATHTYPE]
                         [WIDTH] [STRANS] XY STRING}

<property>  ::= PROPATTR PROPVALUE
```

**중요:** 모든 엘리먼트 앞에 `[ELFLAGS] [PLEX]` 옵션이 올 수 있다. (오프셋 기록용)

---

## 5. 엘리먼트 상세 (Element Types)

### 5.1 BOUNDARY (폴리곤)

IC 레이아웃의 **채워진 다각형(filled polygon)** 을 표현한다.

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | BOUNDARY | ✓ | 엘리먼트 시작 마커 |
| 2 | [ELFLAGS] | | 엘리먼트 플래그 |
| 3 | [PLEX] | | PLEX ID |
| 4 | LAYER | ✓ | 레이어 번호 (0~32767) |
| 5 | DATATYPE | ✓ | 데이터 타입 번호 (0~32767) |
| 6 | XY | ✓ | 좌표 리스트 (최소 4점, 최대 8191점, 첫 점=마지막 점으로 닫힘) |
| 7 | ENDEL | ✓ | 엘리먼트 종료 |

- **점 제약**: 최소 4점 (spec 기준: 최대 600점, 파서: 최대 8191점)
- 첫 번째 점과 마지막 점이 동일해야 폴리곤이 닫힘

### 5.2 PATH (패스/배선)

IC 레이아웃의 **배선(wire/trace)** 을 표현한다.

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | PATH | ✓ | 엘리먼트 시작 |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | LAYER | ✓ | |
| 4 | DATATYPE | ✓ | |
| 5 | [PATHTYPE] | | 끝점 스타일 (0=Flush, 1=Round, 2=Extend, 4=Custom) |
| 6 | [WIDTH] | | 선 폭 (데이터베이스 유닛, 음수=절대값) |
| 7 | [BGNEXTN] | | 시작 연장 길이 (PATHTYPE=4일 때만 유효) |
| 8 | [ENDEXTN] | | 끝 연장 길이 (PATHTYPE=4일 때만 유효) |
| 9 | XY | ✓ | 좌표 리스트 (최소 2점, 최대 8191점) |
| 10 | ENDEL | ✓ | |

**PATHTYPE 상세:**
| 값 | 이름 | 설명 |
|---|---|---|
| 0 | **Flush** | 사각형 끝. 끝점과 일치 (기본값) |
| 1 | **Round** | 반원형 끝. 끝점이 원의 중심 |
| 2 | **Extend** | 사각형 끝. 폭의 절반만큼 연장 |
| 4 | **Custom** | 사각형 끝. BGNEXTN/ENDEXTN으로 길이 지정 |

### 5.3 SREF (단순 구조체 참조)

다른 구조체(셀)를 단일 인스턴스로 참조한다.

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | SREF | ✓ | |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | SNAME | ✓ | 참조할 구조체 이름 |
| 4 | [STRANS] | | 변환 (반사, 절대값 플래그) |
| 5 | [MAG] | | 확대 비율 (STRANS 필요, 기본값 1.0) |
| 6 | [ANGLE] | | 회전 각도 (STRANS 필요, 기본값 0.0) |
| 7 | XY | ✓ | **단일 점** (배치 위치) |
| 8 | ENDEL | ✓ | |

### 5.4 AREF (배열 구조체 참조)

다른 구조체를 **격자 배열**로 참조한다.

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | AREF | ✓ | |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | SNAME | ✓ | 참조할 구조체 이름 |
| 4 | [STRANS] [MAG] [ANGLE] | | SREF와 동일 |
| 5 | COLROW | ✓ | **열 개수, 행 개수** (각 2바이트, 최대 32767) |
| 6 | XY | ✓ | **정확히 3점**: 원점, 행 방향(+X), 열 방향(+Y) 벡터 |
| 7 | ENDEL | ✓ | |

**배열 배치 계산:**
```
각 인스턴스 위치 = origin + col * x_vector + row * y_vector
  where 0 ≤ col < numCols, 0 ≤ row < numRows
```

### 5.5 TEXT (텍스트 레이블)

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | TEXT | ✓ | |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | LAYER | ✓ | |
| 4 | TEXTTYPE | ✓ | 텍스트 타입 번호 |
| 5 | [PRESENTATION] | | 폰트(0~3), 수직정렬(0=Top,1=Middle,2=Bottom), 수평정렬(0=Left,1=Center,2=Right) |
| 6 | [PATHTYPE] | | (TEXT에도 PATHTYPE이 옵션으로 올 수 있음) |
| 7 | [WIDTH] | | 텍스트 폭 |
| 8 | [STRANS] [MAG] [ANGLE] | | 변환 |
| 9 | XY | ✓ | **단일 점** (위치) |
| 10 | STRING | ✓ | 텍스트 내용 |
| 11 | ENDEL | ✓ | |

### 5.6 NODE (노드)

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | NODE | ✓ | |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | LAYER | ✓ | |
| 4 | NODETYPE | ✓ | |
| 5 | XY | ✓ | 좌표 리스트 (최소 1점, 최대 8191점) |
| 6 | ENDEL | ✓ | |

### 5.7 BOX (박스)

| 순서 | 레코드 | 필수 | 설명 |
|---|---|---|---|
| 1 | BOX | ✓ | |
| 2 | [ELFLAGS] [PLEX] | | |
| 3 | LAYER | ✓ | |
| 4 | BOXTYPE | ✓ | |
| 5 | XY | ✓ | 좌표 리스트 (최소 5점, 닫힌 다각형) |
| 6 | ENDEL | ✓ | |

---

## 6. STRANS (변환 플래그) 비트 구조

STRANS는 **2바이트 비트 배열**로, SREF, AREF, TEXT의 변환 정보를 담는다.

```
비트:  15  14  13  12  11  10  9  8  7  6  5  4  3  2  1  0
       ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
       │ R │AM │AA │   │   │   │   │   │   │   │   │   │   │   │   │   │
       └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
```

| 비트 | 플래그 | 설명 |
|---|---|---|
| 15 (0x8000) | **R** (Reflection) | X축 반사 (회전 *전에* 적용) |
| 14 (0x4000) | **AM** (Abs Mag) | MAG이 부모 변환의 영향을 받지 않음 |
| 13 (0x2000) | **AA** (Abs Angle) | ANGLE이 부모 변환의 영향을 받지 않음 |

**변환 적용 순서:**
1. X축 반사 (Reflection)
2. 배율 (Magnification)
3. 회전 (Rotation by ANGLE)
4. 평행이동 (Translation by XY)

---

## 7. 라이브러리 헤더 (Library Header) 상세

### 7.1 HEADER 레코드

GDSII 버전 번호를 담는다. 일반적인 값:

| 버전 | 설명 |
|---|---|
| 0 | GDSII Stream 형식 0 |
| 3 | 가장 일반적인 값 (현대 GDSII 파일) |
| 4 | GDSII Stream 형식 4 |

### 7.2 BGNLIB / BGNSTR — 타임스탬프

타임스탬프는 **6개의 short(2바이트)** 값으로 구성되며, 리틀 엔디안 외의 특별한 변환 없이 직접 읽는다:

| 오프셋 | 필드 | 범위 |
|---|---|---|
| 0-1 | Year | (예: 2004) |
| 2-3 | Month | 1~12 |
| 4-5 | Day | 1~31 |
| 6-7 | Hour | 0~23 |
| 8-9 | Minute | 0~59 |
| 10-11 | Second | 0~59 |

BGNLIB는 이 12B 쌍을 2개 포함 = **24B** (수정 시간 + 접근 시간)
BGNSTR도 12B 쌍을 2개 포함 = **24B** (생성 시간 + 수정 시간)

### 7.3 UNITS 레코드

두 개의 **8바이트 실수** (IBM 370 형식):

| 오프셋 | 크기 | 내용 | 설명 |
|---|---|---|---|
| 0-7 | 8B | **DB→User** | 1 데이터베이스 유닛 = 몇 user units (예: 0.001 = 1nm) |
| 8-15 | 8B | **DB→Meter** | 1 데이터베이스 유닛 = 몇 미터 (예: 1e-9 = 1nm) |

### 7.4 FORMAT / MASK / ENDMASKS

필터링된 GDSII 파일에서 사용:
- **FORMAT=1** (GDSII_Filtered)일 때만 MASK 레코드가 따라옴
- MASK는 문자열로 레이어 필터 범위 지정 (예: `"2-10  12"`)

### 7.5 LIBSECUR — ACL 엔트리

트리플렛 (groupid, userid, access) × N, 각 2바이트 short, 최대 32 엔트리

---

## 8. 제약값 및 한계 (Constants from glimits.h)

### 8.1 범용 제약

| 상수 | 값 | 설명 |
|---|---|---|
| `RecordHeaderLength` | 4 | 각 레코드 헤더 크기 |
| `MaxRecordLength` | 65534 | 레코드 최대 길이 (헤더 포함) |

### 8.2 엘리먼트별 점 제약

| 엘리먼트 | 최소 점 | 최대 점 (파서) | 최대 점 (명세) |
|---|---|---|---|
| BOUNDARY | 4 | 8191 | 600 |
| PATH | 2 | 8191 | 200 |
| NODE | 1 | 8191 | 50 |
| BOX | 5 | 8191 | - |
| XY (일반) | 1 | 8191 | 600 |

### 8.3 값 범위 제약

| 필드 | 명세 범위 | 파서 허용 범위 |
|---|---|---|
| LAYER | 0~255 | 0~32767 |
| DATATYPE | 0~255 | 0~32767 |
| TEXTTYPE | 0~255 | 0~32767 |
| NODETYPE | 0~255 | 0~32767 |
| BOXTYPE | 0~255 | 0~32767 |
| PROPATTR | 1~127 | 0~127 |
| 구조체 이름 | 32자 | 512자 (파서: 65530B까지) |
| 텍스트 문자열 | 512자 | 512자 |
| PROPATTR 값 | 126B | 126B |
| AREF COLROW | - | 0~32767 |

### 8.4 기타 상수

| 상수 | 값 | 설명 |
|---|---|---|
| MaxAttrTableLength | 44 | ATTRTABLE 최대 경로명 길이 |
| MaxFontNameLength | 44 | 폰트 파일명 최대 길이 |
| MinReflibs / MaxReflibs | 2 / 17 | REFLIBS 라이브러리 개수 범위 |
| ReflibNameLength | 44 | 각 REFLIBS 이름 고정 길이 |
| MaxAclEntries | 32 | LIBSECUR 최대 엔트리 수 |
| MinGenerations / MaxGenerations | 2 / 99 | GENERATIONS 범위 |
| BufferSize (스캐너) | 131072 (128KB) | 내부 읽기 버퍼 크기 |

---

## 9. 파서 확장 (Parser Extensions)

이 코드베이스(SoftJin libgdsii)의 파서는 공식 GDSII 명세보다 **더 관대하게** 동작한다:

| 항목 | GDSII 명세 | 파서 허용 |
|---|---|---|
| 구조체 이름 문자 | 영숫자, `_`, `?`, `$`만 | 모든 가시 ASCII 문자 |
| 구조체 이름 길이 | 32자 | 65530B |
| LIBNAME | 필요 | 빈 문자열(0B)도 허용 |
| DATATYPE (BOUNDARY/PATH) | 필수 | 생략 시 0으로 간주 |
| XY 점 개수 (BOUNDARY) | 최대 600 | 최대 8191 (레코드 한계) |
| XY 점 개수 (PATH) | 최대 200 | 최대 8191 |
| GENERATIONS | 2~99 | 모든 값 허용 |
| MAG와 ANGLE 순서 | MAG 먼저 | 순서 바뀜 허용 |
| PATHTYPE | 0,1,2,4만 | 잘못된 값 → 0 처리 |
| PROPATTR | 값 중복 불가, 0 금지 | 중복/0 모두 허용 |
| PROPVALUE 길이 | 126B 제한 | 없음 (레코드 최대까지) |
| ATTRTABLE | 내용 필요 | 빈 내용 허용 |
| FONTS | 최소 1개 필요 | 미체크 |

---

## 10. ASCII 표현 형식 (AscGDS)

GDSII는 바이너리 파일이므로 디버깅을 위해 **ASCII 텍스트 표현**을 제공한다.

### 10.1 기본 규칙

| 항목 | 규칙 |
|---|---|
| 주석 | `#`부터 줄 끝까지 |
| 구분자 | 공백 (탭/스페이스) |
| 줄바꿈 | 무시됨 (공백과 동일) |
| 대소문자 | **구분하지 않음** (record names) |
| 정수 (2B/4B) | C 형식 (10진, 16진 `0x`, 8진 `0`) |
| 비트 배열 | 정수로 표현 (음수 불가) |
| 실수 | C 형식 부동소수점 |
| 문자열 | 큰따옴표(`"..."`), 이스케이프: `\"`, `\\`, `\ooo` (8진수) |

### 10.2 특수 레코드 형식

```
XY    n  x1 y1 x2 y2 ... xn yn       // n=점 개수, 좌표 쌍
LIBSECUR  n  g1 u1 a1 ... gn un an    // n=ACL 엔트리 개수, 그룹 사용자 권한 트리플렛
REFLIBS  n  "lib1" "lib2" ... "libn"  // n=라이브러리 개수, 문자열 리스트
FONTS  "f1" "f2" "f3" "f4"           // 정확히 4개 문자열 (빈 문자열 가능)
```

### 10.3 샘플 (sample.ascgds)

```gds
header 3
bgnlib 104 08 01 12 0 0   104 08 01 12 0 0
libname "sample.gds"
units .001 1e-9

bgnstr  104 08 01 12 0 0   104 08 01 12 0 0
strname "cell1"

boundary
    layer 42
    datatype 10
    xy  7  0 0  -10 -20  -100 0  -50 80  50 50  0 50   0 0
    endel
path
    layer 1
    datatype 1
    pathtype 2
    xy  10  0 0  1 0  1 1  2 1  2 0  3 0  3 1  4 1  4 0  5 0
    endel
endstr

endlib
```

---

## 11. 소프트웨어 아키텍처 (libgdsii 구조)

이 코드베이스는 **3계층 구조**로 설계되어 있다:

```
Layer 2 (고수준): parser.cc, builder.cc, creator.cc, asc-conv.cc
     └── Builder 패턴: GdsParser → GdsBuilder → GdsCreator
     └── Event-driven (SAX-like) 파싱
     
Layer 1 (중간): writer.cc, scanner.cc, asc-scanner.l, asc-writer.cc, double.cc
     └── 레코드 단위 읽기/쓰기
     └── IBM 370 ↔ IEEE 754 변환

Layer 0 (저수준): rectypes.cc, rectypes.h, glimits.h
     └── 레코드 타입 정의 (GdsRecordType, GdsDataType)
     └── GdsRecordTypeInfo 배열 (70개 레코드의 메타정보)
```

### 11.1 주요 클래스

| 클래스 | 헤더 | 역할 |
|---|---|---|
| `GdsScanner` | scanner.h | 저수준: GDSII 바이너리 파일 → 레코드 스트림 |
| `GdsRecord` | scanner.h | 단일 레코드: 타입, 데이터 접근자 (nextShort/nextInt/nextDouble/nextString) |
| `GdsWriter` | writer.h | 저수준: 레코드 스트림 → GDSII 바이너리 파일 |
| `GdsParser` | parser.h | 고수준: GDSII 파일 → 구조체/엘리먼트/프로퍼티 이벤트 |
| `GdsBuilder` | builder.h | 콜백 인터페이스 (beginBoundary, beginPath, beginSref 등 22개 가상 메서드) |
| `GdsCreator` | creator.h | GdsBuilder 상속 → GDSII 파일 생성 (writer 래퍼) |
| `GdsToAsciiConverter` | asc-conv.cc | GDSII → ASCII 변환 |
| `AsciiToGdsConverter` | asc-conv.cc | ASCII → GDSII 변환 |
| `GdsRecordTypeInfo` | rectypes.h | 각 레코드 타입의 메타정보 (데이터타입, 길이 제약, 이름) |
| `GdsLocator` | locator.h | 파싱 위치 추적 (파일명, 구조체명, 오프셋) |
| `FileIndex` | file-index.h | 구조체 이름 → 파일 오프셋 매핑 (랜덤 액세스) |

### 11.2 GdsBuilder 콜백 호출 순서

```
setLocator()
gdsVersion(int)
beginLibrary(name, modTime, accTime, units, options)
    beginStructure(name, createTime, modTime, options)
        beginBoundary(layer, datatype, points, options)
            addProperty(attr, value)
        endElement()
        beginPath(layer, datatype, points, options) ...
        beginSref(sname, x, y, strans, options) ...
        beginAref(sname, numCols, numRows, points, strans, options) ...
        beginNode(layer, nodetype, points, options) ...
        beginBox(layer, boxtype, points, options) ...
        beginText(layer, texttype, x, y, text, strans, options) ...
    endStructure()
endLibrary()
```

---

## 12. 구조체 참조 그래프 (Structure DAG)

GDSII 구조체는 **방향성 비순환 그래프(DAG)** 를 이룬다:

- 각 **구조체(셀)** 는 0개 이상의 **SREF/AREF**를 통해 다른 구조체를 참조
- **순환 참조는 허용되지 않음** (순환은 애플리케이션 단에서 처리)
- `GdsParser::buildStructureGraph()`는 DAG 빌더에게 순회 이벤트 전달:

```
beginLibrary("library_name")
    enterStructure("cellA")    { addSref("cellB"), addSref("cellC") }
    enterStructure("cellB")    { addSref("cellD") }
    enterStructure("cellC")    { addSref("cellD") }
    enterStructure("cellD")    {}
endLibrary()
```

---

## 13. 애플리케이션 도구

이 코드베이스에 포함된 유틸리티 도구들:

| 도구 | 설명 |
|---|---|
| `gds-recstats` | GDSII 파일의 레코드 타입별 통계 출력 |
| `gds-copy` | GdsParser + GdsCreator로 파일 복사 (파이프라인 테스트) |
| `gds2ascii` | GDSII 바이너리 → ASCII 텍스트 변환 |
| `ascii2gds` | ASCII 텍스트 → GDSII 바이너리 변환 |
| `gds-cells` | GDSII 파일의 모든 구조체 이름 출력 |
| `gds-dot` | 구조체 참조 그래프 → Graphviz DOT 형식 출력 |

---

## 14. 참고 자료

- 공식 GDSII Stream Format Specification (검색: "gdsii stream format")
- [anuvad 프로젝트](https://github.com/kdr7918/anuvad) — 이 문서의 분석 기준 소스코드
- SoftJin libgdsii — C++ GDSII 라이브러리 (BSD-style 라이선스)
- GDSII는 Calma사가 개발, 현재는 Cadence Design Systems가 소유

---

*이 문서는 anuvad 프로젝트의 `src/gdsii/` 소스코드 분석을 기반으로 작성되었습니다.*
*GDSII Stream Format (Calma GDSII) — 반도체 IC 레이아웃의 사실상 표준(de facto standard) 바이너리 포맷*
