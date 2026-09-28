# 저장된 Property를 Creator로 출력

추가할 파일은 [`property-creator.h`](property-creator.h) 하나입니다.
기존 PropertyStore는 변경하지 않습니다. Multigon의 Creator/이름 타입 헤더가 필요합니다.

## 변환 흐름

```text
store.getShape(shape.base())
    -> PropertyList
    -> 출력할 때만 Oasis::Property로 변환
    -> creator.addElementProperty()
```

## 붙여넣을 사용 예시

아래는 이미 Creator로 파일/Cell/도형을 쓰는 코드에 넣는 조각입니다.
`creator`, `store`, `shape` 및 좌표 변수는 기존 출력 코드의 객체를 사용합니다.

```cpp
#include "property-creator.h"

// 파일 출력 전체 동안 유지. 루프 안에서 만들지 마세요.
layout_property::OutputNames names;

// 기존 creator.beginFile(...);
// 기존 creator.beginCell(...);

// 기존 도형 출력
creator.beginRectangle(
    layer, datatype, x, y, width, height, nullptr);

// 방금 출력한 도형의 Property를 바로 뒤에 출력
layout_property::writeElementProperties(
    creator,
    store.getShape(shape.base()),
    names);

creator.endElement();

// 다른 도형도 begin...() 직후 같은 함수를 호출합니다.
// 모두 같은 names를 사용합니다.
creator.endCell();
creator.endFile();
// 이 시점 이후 names 파괴 가능
```

## 보존하는 것

- 입력 이름과 standard flag
- Property 순서와 중복 이름
- 값의 순서, signed/unsigned, 문자열 종류와 binary NUL
- 유리수 분자/분모 및 Float/Double 구분

이름은 출력 수명 동안 names가 소유합니다. 임시 Oasis::Property 자체는 각 호출 후
파괴할 수 있습니다. Creator가 파일 인코딩/재사용 표현을 처리합니다.

## 제한

- 이 함수는 **OASIS 이름 기반 Element Property**용입니다.
- GDS 속성은 명시적으로 거부합니다. S_GDS_PROPERTY 변환은 포함하지 않았습니다.
- 유리수는 현재 Creator의 Oreal이 표현할 수 있는 long 범위를 검사합니다.
- source.standard가 true이면 해당 이름/값/출력 위치가 표준 규칙에 맞아야 합니다.
- Repetition은 base를 꺼내기 전 wrapper 주소로 Property를 조회합니다.
- 변환/출력 중 예외가 나면 파일 출력 전체를 실패로 처리하세요. 이전 출력의 롤백은 없습니다.
- 원본 파일의 refnum·modal 표현·바이트 배열까지 재현하는 것이 아닙니다.

실제 Multigon 헤더로 C++11 문법 검사를 수행한 예제입니다.
파일을 실제로 쓰고 다시 읽는 roundtrip 검증은 아직 하지 않았습니다.
