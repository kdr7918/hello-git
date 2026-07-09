# GDSII 도형 타입별 기하학적 의미 정리

> Anuvad 프로젝트 `src/gdsii` 구현을 기준으로 정리.
> 주요 참조 소스: `src/gdsii/rectypes.h`(레코드 타입 열거), `src/gdsii/builder.h`(요소 빌더 / `GdsPathtype`),
> `src/gdsii/glimits.h`(좌표점 수 제한), `src/gdsii/parser.h`(파싱 규칙).

GDSII(Stream format)에서 실제 **2차원 기하 도형을 표현하는 요소(element)** 는
다음 7종이다. 나머지 레코드(LIBNAME, LAYER, XY, UNITS 등)는 이 요소들을
감싸는 메타데이터/컨테이너다.

| 요소 레코드 | ID | 기하학적 의미 |
|---|---|---|
| BOUNDARY | 8 | 닫힌 다각형(폴리곤) 영역 — 채워진 면 |
| PATH | 9 | 중심선 + 폭을 가진 선분 체인(스트로크된 선) |
| SREF | 10 | 단일 구조 참조(인스턴스) — 점 변환만 |
| AREF | 11 | 배열 구조 참조 — 격자(grid)로 반복 배치 |
| TEXT | 12 | 문자열을 기하 위치에 렌더링 |
| NODE | 21 | 연결 노드(전기적 넷 접점) — 기하적 면적 없음 |
| BOX | 45 | 직사각형 전용 경량 면(축정렬 사각형) |

---

## 1. BOUNDARY (ID 8) — 채워진 다각형

**기하학적 의미**
닫힌 선분 체인으로 둘러싸인 **내부가 채워진(filled) 2차원 면적 영역**이다.
레이아웃에서 확산 영역, 금속 배선 패드, 웰 등 "면"으로 존재하는 패턴을 표현한다.

**구성 레코드**
- `LAYER`(13) — 층 번호 (0..255)
- `DATATYPE`(14) — 데이터 타입 (0..255, purpose 구분용)
- `XY`(16) — 정점 좌표 리스트. **첫 점과 마지막 점이 자동으로 닫힌다.**
  (GDSII 명세상 XY 마지막 점은 첫 점과 동일하게 닫힘 처리)

**좌표점 수 제한** (`glimits.h`)
- 최소 4점, 최대 8191점 (파서 수용 한계)
- 명세 권장 최대: 600점 (`MaxBoundaryPointsSpec`)

**코드 참조**
- `rectypes.h:33` `GRT_BOUNDARY = 8`
- `builder.h:744` `beginBoundary(int layer, int datatype, ...)`
- `glimits.h:55-57` `MinBoundaryPoints=4`, `MaxBoundaryPoints=8191`

**기하 예시**
```
LAYER 1 / DATATYPE 0
XY: (0,0) (1000,0) (1000,500) (0,500)  → 닫힌 사각형 면적
```

---

## 2. PATH (ID 9) — 중심선 + 폭 선분

**기하학적 의미**
연속된 정점(중심선)을 따라 **일정 폭(width)을 가진 선(stroke)** 을 그린다.
선 자체가 면적을 가지며, 폭과 끝 처리(end-cap) 방식에 따라 기하 형태가 결정된다.
금속 배선, 폴리 게이트 라인 등 "선" 패턴을 표현한다.

**구성 레코드**
- `LAYER`(13), `DATATYPE`(14) — 필수
- `WIDTH`(15) — 선의 **전체 폭**(중심선 양쪽으로 절반씩)
- `PATHTYPE`(33) — 끝단 처리 방식 (아래 4종)
- `XY`(16) — 2점 이상의 중심선 정점
- `BGNEXTN`(48) / `ENDEXTN`(49) — PATH가 Custom(4)일 때만 유효한 양 끝 연장 길이

**PATHTYPE (끝단 기하, `builder.h:67` `GdsPathtype`)**
| 값 | 이름 | 기하학적 끝단 형태 |
|---|---|---|
| 0 | `PathtypeFlush` | 끝점에서 수직으로 잘린 사각 끝(default) |
| 1 | `PathtypeRound` | 끝점을 중심으로 한 반원(semicircle) 캡 |
| 2 | `PathtypeExtend` | 끝점에서 폭의 절반만큼 사각 연장 |
| 4 | `PathtypeCustom` | 사각 끝 + `BGNEXTN`/`ENDEXTN`으로 임의 연장 |

> 참고: 값 3은 사용되지 않음(주석 "PathtypeCustom is 4, not 3").

**좌표점 수 제한** (`glimits.h`)
- 최소 2점, 최대 8191점
- 명세 권장 최대: 200점 (`MaxPathPointsSpec`)

**코드 참조**
- `rectypes.h:34` `GRT_PATH = 9`
- `builder.h:748` `beginPath(...)`
- `builder.h:515-547` `GdsPathOptions` (pathtype/widthextn 필드)
- `glimits.h:63-65`

---

## 3. SREF (ID 10) — 단일 구조 참조(인스턴스)

**기하학적 의미**
다른 구조(cell)를 **한 번** 배치하는 참조(인스턴스)다. 자기 자신 기하를
담지 않고, 참조 대상 구조의 기하를 **변환(위치 이동/회전/반사/축척)** 하여
현 위치에 놓는다. 계층(hierarchy) 설계의 핵심.

**구성 레코드**
- `SNAME`(18) — 참조할 구조 이름
- `STRANS`(26) — 변환 플래그(반사/절대배율 등)
- `MAG`(27) — 축척 배율 (옵션)
- `ANGLE`(28) — 회전각 (옵션)
- `XY`(16) — 배치 위치(placement point) — 보통 1점

**기하 특성**
- 점 변환만 가능(배열 없음). 격자 반복이 필요하면 AREF 사용.
- 실제 도형 기하는 참조 대상 구조에 있음.

**코드 참조**
- `rectypes.h:35` `GRT_SREF = 10`
- `builder.h:752` `beginSref(const char* sname, ...)`
- `parser.h:94-113` 구조 트리/참조 설명

---

## 4. AREF (ID 11) — 배열 구조 참조

**기하학적 의미**
구조를 **N×M 격자(grid)** 로 반복 배치하는 참조다. 한 번의 정의로
수백~수만 개 인스턴스를 격자형으로 찍어낸다(메모리/파일 크기 절약).

**구성 레코드**
- `SNAME`(18) — 참조 구조
- `COLROW`(19) — 열(column) 수, 행(row) 수
- `STRANS`(26) / `MAG`(27) / `ANGLE`(28)
- `XY`(16) — **3점**: (기준점, 열 방향 벡터 끝, 행 방향 벡터 끝)
  - 열 간격 = (점2 - 점1) / (col-1)
  - 행 간격 = (점3 - 점1) / (row-1)

**기하 특성**
격자 스텝은 두 벡터로 정의되므로 **비정사각/임의 회전 격자**도 표현 가능.

**코드 참조**
- `rectypes.h:37` `GRT_AREF = 11`
- `builder.h:757` `beginAref(const char* sname, ...)`
- `rectypes.h:45` `GRT_COLROW = 19`

---

## 5. TEXT (ID 12) — 문자열 기하 배치

**기하학적 의미**
문자열을 지정 위치에 **텍스트로 렌더링** 한다. 기하적 "면적"보다는
레이아웃 주석/라벨 용도. 폰트/표시 속성에 따라 실제 글자 외곽선 기하가 결정.

**구성 레코드**
- `LAYER`(13), `TEXTTYPE`(22)
- `PRESENTATION`(23) — 폰트/방향/표시 비트
- `PATHTYPE`(33) — 텍스트 경로 타입(일부 툴에서 외곽선 스타일)
- `STRANS`(26) / `MAG`(27) / `ANGLE`(28) — 변환
- `STRING`(25) — 실제 문자열
- `XY`(16) — 앵커 위치 (보통 1점)

**코드 참조**
- `rectypes.h:38` `GRT_TEXT = 12`
- `builder.h:556-616` `GdsTextOptions` (presentation/pathtype)
- `builder.h:771` `beginText(int layer, int textType, ...)`

---

## 6. NODE (ID 21) — 연결 노드

**기하학적 의미**
전기적 넷(net)의 **연결 접점** 을 표시한다. 면적/선폭을 갖지 않는
"위치 마커" 성격. 물리 기하보다는 회로 연결 의미(connectivity)를 담는다.

**구성 레코드**
- `LAYER`(13), `NODETYPE`(42)
- `XY`(16) — 접점 좌표(1점 또는 소수점)

**코드 참조**
- `rectypes.h:48` `GRT_NODE = 21`
- `builder.h:763` `beginNode(int layer, int nodetype, ...)`

---

## 7. BOX (ID 45) — 직사각형 전용 면

**기하학적 의미**
**축 정렬된 직사각형(rectangle)** 만을 표현하는 경량 면 요소.
BOUNDARY로 사각형을 그리는 것보다 파일/파싱 효율이 높다(명세 후기 추가).
사각형 마스크/패드를 빠르게 표현할 때 사용.

**구성 레코드**
- `LAYER`(13), `BOXTYPE`(46)
- `XY`(16) — 5점(사각형 4꼭짓점 + 닫힘 복제, 또는 4점+닫힘)

**코드 참조**
- `rectypes.h:74` `GRT_BOX = 45`, `rectypes.h:75` `GRT_BOXTYPE = 46`
- `builder.h:767` `beginBox(int layer, int boxtype, ...)`
- `glimits.h:82` BOXTYPE 범위 0..255

---

## 부록: 도형에 공통으로 쓰이는 기하 관련 레코드

| 레코드 | ID | 역할 |
|---|---|---|
| LAYER | 13 | 층(plane) 번호 — 기하가 속한 물리 층 |
| DATATYPE / TEXTTYPE / NODETYPE / BOXTYPE | 14/22/42/46 | purpose/서브타입 |
| XY | 16 | 좌표점 리스트(모든 도형의 기하 원점) |
| WIDTH | 15 | PATH 전체 폭 |
| PATHTYPE | 33 | PATH/TEXT 끝단·경로 처리 |
| STRANS | 26 | SREF/AREF/TEXT 변환(반사/절대배율) |
| MAG / ANGLE | 27/28 | 축척 / 회전 |
| BGNEXTN / ENDEXTN | 48/49 | PATH 끝 연장 길이(custom 전용) |

---

## 요약

- **면(area)**: BOUNDARY(임의 다각형), BOX(직사각형 전용)
- **선(line)**: PATH(중심선+폭, end-cap 4종)
- **참조(reference)**: SREF(단일), AREF(격자 배열) — 기하는 참조 대상에 있음
- **주석/연결**: TEXT(문자열), NODE(넷 접점, 면적 없음)

모든 도형의 실제 좌표는 `XY` 레코드에 있고, 층/용도는 `LAYER`+`*TYPE` 으로 나뉜다.
Anuvad 파서는 BOUNDARY 최대 8191점 / PATH 최대 8191점(권장 600/200)을 수용한다.
