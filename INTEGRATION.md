# Multigon에 붙여넣을 추가 코드

완성 패치가 아닌 **추가 위치별 조각**입니다. 기존 함수 전체를 대체하지 않습니다.
원본 Multigon 소스는 이 저장소에 포함하지 않습니다.

## 1. 새 파일 복사

`property.h`, `property-store.h`, `oasis-property-adapter.h`를
`src/oasis/layout/`에 복사합니다. `property-usage.cc`는 참고용입니다.
namespace는 기존 `Oasis::Property`와 충돌하지 않는 `property_example`입니다.

## 2. Layout: 저장소 하나 추가

`src/oasis/layout/layout.h`의 include 영역:

```cpp
#include "property-store.h"
```

기존 `Layout` 클래스 public 영역:

```cpp
property_example::PropertyStore& propertyStore() { return properties_; }
const property_example::PropertyStore& propertyStore() const { return properties_; }
```

private 영역:

```cpp
property_example::PropertyStore properties_;
```

기존 Shape/Cell/TaggedPtr 구조체에는 필드를 추가하지 않습니다.
Store는 mutex 때문에 복사/이동 불가입니다. Layout이 직접 값으로 복사/이동되는
사용처가 있다면 그 계약부터 확인해야 합니다. 이 코드는 일회성 Import용입니다.

## 3. OasisLayoutBuilder: 현재 대상과 임시 목록

`src/oasis/layout/builder.h` include 영역:

```cpp
#include "property.h"
```

기존 클래스 public 영역:

```cpp
void addFileProperty(Oasis::Property* prop) override;
void addCellProperty(Oasis::Property* prop) override;
void addElementProperty(Oasis::Property* prop) override;
```

private 영역:

```cpp
const ShapeBase* propertyOwner_ = nullptr;
property_example::PropertyList fileProperties_;
property_example::PropertyList cellProperties_;
property_example::PropertyList elementProperties_;
```

`builder.cc` include 영역:

```cpp
#include "oasis-property-adapter.h"
```

기존 `addShape(LayerInfo li, TaggedPtr sp)`의 **첫 줄**에 추가합니다.
Placement 분기의 early return보다 앞이어야 합니다.

```cpp
propertyOwner_ = sp.base();
```

최종 addShape에 전달되는 객체를 기억하므로 일반 도형/Placement는 해당 주소,
Repetition은 wrapper 주소가 됩니다. point-list/blob 주소를 key로 쓰지 않습니다.

새 콜백 구현:

```cpp
void OasisLayoutBuilder::addFileProperty(Oasis::Property* prop)
{
    property_example::appendOasis(layout_->propertyStore(), fileProperties_, *prop);
}

void OasisLayoutBuilder::addCellProperty(Oasis::Property* prop)
{
    property_example::appendOasis(layout_->propertyStore(), cellProperties_, *prop);
}

void OasisLayoutBuilder::addElementProperty(Oasis::Property* prop)
{
    if (!propertyOwner_) return; // 저장하지 않는 TEXT/X 등의 속성도 생략
    property_example::appendOasis(layout_->propertyStore(), elementProperties_, *prop);
}
```

기존 `endElement()` 본문에 추가:

```cpp
if (propertyOwner_ && elementProperties_.count != 0)
    layout_->propertyStore().setElement(propertyOwner_, elementProperties_);
propertyOwner_ = nullptr;
elementProperties_.clear();
```

기존 `beginCell()`에 추가:

```cpp
propertyOwner_ = nullptr;
elementProperties_.clear();
cellProperties_.clear();
```

기존 `endCell()`에서 currentCellId_를 초기화하기 **전**에 추가:

```cpp
if (cellProperties_.count != 0)
    layout_->propertyStore().setCell(currentCellId_, cellProperties_);
cellProperties_.clear();
```

기존 `beginFile()`에 추가:

```cpp
fileProperties_.clear();
```

기존 `endFile()`에 추가 (전체 파일 단일 Builder에서만 실행):

```cpp
layout_->propertyStore().setFile(fileProperties_);
fileProperties_.clear();
```

생략되는 `beginText()`, `beginXElement()`, `beginXGeometry()` 각각의 첫 부분:

```cpp
propertyOwner_ = nullptr;
elementProperties_.clear();
```

이러한 reset이 없으면 생략 요소의 속성이 직전 도형에 잘못 붙을 수 있습니다.
원래 경고 함수와 기존 동작은 그대로 둡니다.

## 4. GdsLayoutBuilder: no-op addProperty 교체

`src/layout-tools/gds-layout.h`에 위의 propertyOwner_/elementProperties_ 멤버를
같이 추가하고, 해당 .cc에 adapter 헤더를 include합니다.
이 Builder의 addShape 첫 줄에도 `propertyOwner_ = sp.base();`를 추가합니다.

```cpp
void GdsLayoutBuilder::addProperty(int attr, const char* value)
{
    if (!propertyOwner_) return;
    property_example::appendGds(elementProperties_, attr, std::string(value));
}

void GdsLayoutBuilder::endElement()
{
    if (propertyOwner_ && elementProperties_.count != 0)
        layout_->propertyStore().setElement(propertyOwner_, elementProperties_);
    propertyOwner_ = nullptr;
    elementProperties_.clear();
}
```

beginCell과 생략되는 TEXT/NODE 등의 begin 콜백에서 owner/buffer를 초기화합니다.
GDS 콜백의 NUL-terminated string 계약을 사용하므로 원본 padding 보존은 아닙니다.

## 5. freeze 위치 — 중요

단일/병렬 공통으로 **전체 Import 완료, 모든 worker join 이후** 한 번만 실행합니다.
`src/sdk/api.cc`의 `Importer::read()`에서 `Layout result`를 반환하기 전에 넣습니다.
완료 progress 콜백 전에 수행해야 freeze 실패를 성공으로 보고하지 않습니다.

```cpp
state->layout->propertyStore().freeze();
```

**worker endFile/endCell에서 freeze하지 않습니다.**
Store는 잠금으로 intern/append를 보호하지만 전역 lock 병목을 측정하지 않았습니다.

병렬 Import의 coordinator/index builder에도 파일 Property 수집 경로가 필요합니다.
단일 Builder의 addFileProperty만 추가해 놓고 병렬도 된다고 간주하지 마세요.
Coordinator에서 file 목록을 모아 `setFile()`을 정확히 한 번 호출하고,
worker는 자신의 Cell/Element 목록만 저장합니다.

`OasisImportBuilder` 등의 SDK subclass가 geometry begin을 우회하는
`importShapes == false` 경로에서는 Shape Property를 저장할 owner가 없습니다.
이 예제에서는 미수입으로 취급하고 SDK 조회 시 DataNotImported를 반환하도록
별도로 연결해야 합니다. 빈 목록과 미수입을 동일시하지 않습니다.

## 6. 특정 Shape의 Property 조회

Import 완료 후의 내부 코드:

```cpp
const auto& store = layout.propertyStore();
const auto list = store.element(shape.base());
store.forEach(list, [&](property_example::PropertyView property) {
    if (property.keyKind == property_example::KeyKind::OasisName) {
        const std::string& name = store.name(property.key);
        // name 사용
        (void)name;
    } else {
        const uint64_t gdsAttribute = property.key;
        (void)gdsAttribute;
    }
    property.forEachValue([&](property_example::ValueView value) {
        // kind에 따라 value.word(), signedWord(...), stringCopy() 등 사용.
        // Rational/float decoding 예시는 property-usage.cc 참조.
        (void)value;
    });
});
```

Cell은 `store.cell(cellId)`, File은 `store.file()`로 목록을 찾습니다.
각 목록 내부 Property와 value는 입력 순서를 보존합니다.
Repetition 내부 base를 꺼내기 전 wrapper의 주소로 찾아야 합니다.
주소는 Layout 생존 기간의 내부 key이며 파일/다른 Layout으로 전달할 ID가 아닙니다.

## 포함하지 않은 범위

- 실제 Multigon SDK Property API 및 Writer 연결/roundtrip
- CELLNAME/PROPNAME 등 name 레코드 자체에 붙은 Property (CELL 속성과 별개)
- TEXT/X/NODE 자체 및 해당 Property 보존
- Property 편집/삭제, Shape 교체, 재import/reset, lazy import 이후 추가
- blob dedup, 최적화된 varint, 실제 데이터 성능 수치
- 앱 에러 타입으로의 std::exception 변환 위치 정리

저장소는 append -> freeze -> read-only 계약입니다. Layout 파괴/수정과 조회를
동시에 수행하지 않습니다. forEach view는 frozen store 수명에 종속됩니다.

## 구현 표현 메모

Property 클래스는 **임시 serializer**입니다. 이전 대화의 offset 보관용
Property 객체를 다시 도형마다 할당하는 구조가 아닙니다. Store에는 bytes와
PropertyListRef, address -> listId의 정렬 vector만 남습니다.

이름 intern용 hash map만 build 중 존재하고 freeze 때 해제됩니다.
이름 문자열 pool, 각 owner의 연결 항목 및 list descriptor 비용은 남습니다.
정렬 vector를 채택한 예시이며 Multigon의 최종 성능 정책을 바꾼 것은 아닙니다.
