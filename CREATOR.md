# File / Cell / Shape Property를 Creator로 출력

추가할 파일은 [`property-creator.h`](property-creator.h) 하나입니다.
저장 구조는 그대로이며, 출력할 때만 Oasis::Property로 변환합니다.

## 대상별 함수

```cpp
// 파일: beginFile() 직후, 첫 Cell 전에
writeFileProperties(creator, store.getFile(), names);

// Cell: beginCell() 직후, 첫 도형 전에
writeCellProperties(creator, store.getCell(cellId), names);

// Shape/Placement: 해당 begin...() 직후
writeElementProperties(creator, store.getShape(shape.base()), names);
```

세 함수는 `layout_property` namespace 안에 있습니다.
변환 코드는 공통으로 사용하고 마지막 Creator 호출만 다릅니다.

## 목록을 저장하는 방법

```cpp
using namespace layout_property;

Property fileProperty;
fileProperty.name = "author";
fileProperty.addText("Kim");
store.saveFile(PropertyList{fileProperty});

Property cellProperty;
cellProperty.name = "cell_note";
cellProperty.addText("TOP cell");
store.saveCell(cellId, PropertyList{cellProperty});

// Shape도 store.saveShape(shape.base(), list)로 저장
store.finish(); // 모든 file/cell/shape 저장 완료 후 한 번
```

## 출력 순서 — 기존 출력 코드에 삽입

`creator`, `store`, `cellName`, `cellId`, `shape` 및 좌표 변수는 기존 객체입니다.
아래는 독립 프로그램이 아닌 붙여넣기 조각입니다.

```cpp
#include "property-creator.h"
using namespace layout_property;

OutputNames names; // 파일 전체 동안 유지. Cell/Shape 루프 밖에 둡니다.

creator.beginFile("1.0", unit, validationScheme);
writeFileProperties(creator, store.getFile(), names);

creator.beginCell(cellName);
writeCellProperties(creator, store.getCell(cellId), names);

creator.beginRectangle(layer, datatype, x, y, width, height, nullptr);
writeElementProperties(creator, store.getShape(shape.base()), names);
creator.endElement();

// 다른 도형도 begin...() 직후 Property 출력
creator.endCell();

// 다른 Cell도 beginCell() 직후 Property 출력
creator.endFile();
// 이제 names를 파괴해도 됩니다.
```

## 수명과 보존 범위

- 이름 객체는 names가 소유하며 creator.endFile() 반환까지 유지합니다.
- 임시 Oasis::Property는 각 출력 호출 후 파괴할 수 있습니다.
- 이름·standard flag·Property/값 순서·중복 이름·타입을 변환합니다.
- OASIS 파일 인코딩과 modal 재사용 표현은 기존 Creator가 처리합니다.

## 제한

- OASIS 이름 기반 File/Cell/Element용입니다. CELLNAME 레코드 자체의 속성과
  CELL 속성은 다릅니다. 이 함수는 CELLNAME Property를 출력하지 않습니다.
- GDS 속성은 명시적으로 거부합니다. S_GDS_PROPERTY 변환은 포함하지 않습니다.
- standard 속성은 이름/값/출력 위치가 표준 규칙에 맞아야 합니다.
- 유리수는 현재 Creator의 Oreal이 표현할 수 있는 long 범위를 검사합니다.
- Repetition은 base를 꺼내기 전 wrapper 주소로 Property를 조회합니다.
- 출력 중 예외가 나면 파일 전체를 실패로 처리하세요. 이전 출력의 롤백은 없습니다.
- 원본 파일의 refnum·modal 표현·바이트 배열까지 재현하는 것이 아닙니다.

## 실제 검증

실제 Multigon의 liboasis/libmisc에 링크한 strict C++11 테스트로:
1. store에 File/Cell/Shape 속성을 각각 저장
2. 세 출력 함수로 OASIS 파일 생성
3. OasisParser로 재입력
4. 각각 올바른 File/Cell/Element 콜백으로 전달되는지 확인
5. 이름·정수값·binary NUL 문자열 확인

```text
PASS: File/Cell/Element Creator roundtrip; names, integer, binary NUL, scopes
```

이 검증은 위 소규모 fixture 범위입니다. 모든 타입/표준 속성/실파일 corpus 및
Multigon SDK 전체 경로를 검증했다는 뜻은 아닙니다. Multigon 원본은 수정하지 않았습니다.
