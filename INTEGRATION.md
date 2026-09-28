# 어디에 붙여넣나요?

기존 함수는 지우지 말고 아래 코드만 추가합니다. **실제 Multigon에는 아직 적용하지 않았습니다.**

## 1. 파일 복사

이 저장소의 `property.h`, `property-store.h`, `property-store.cc`,
`oasis-property-adapter.h`, `detail/`을 `src/oasis/layout/`에 복사합니다.
빌드 대상에 `property-store.cc`를 추가합니다. 예제 파일은 복사하지 않아도 됩니다.

## 2. Layout에 저장소 하나 추가

`layout.h` include:
```cpp
#include "property-store.h"
```

Layout public 영역 (예제를 쉽게 읽기 위해 public 멤버로 표시):
```cpp
layout_property::PropertyStore properties;
```

Shape/Cell/TaggedPtr에는 필드를 추가하지 않습니다.

## 3. OasisLayoutBuilder에 현재 Shape와 목록 추가

`builder.h`에 `property.h`를 include하고 private 영역에 추가:
```cpp
const void* propertyShape_ = nullptr;
layout_property::PropertyList shapeProperties_;
layout_property::PropertyList cellProperties_;
layout_property::PropertyList fileProperties_;
```

public 영역:
```cpp
void addElementProperty(Oasis::Property* prop) override;
void addCellProperty(Oasis::Property* prop) override;
void addFileProperty(Oasis::Property* prop) override;
```

`builder.cc` include:
```cpp
#include "oasis-property-adapter.h"
```

## 4. 도형 생성 시 주소 기억

기존 `addShape()`의 **첫 줄**, 어떤 early return보다 앞:
```cpp
propertyShape_ = sp.base();
```

Repetition은 최종 wrapper 주소가 기억됩니다. payload 주소를 쓰지 않습니다.

## 5. Property를 받으면 목록에 추가

새 콜백 구현:
```cpp
void OasisLayoutBuilder::addElementProperty(Oasis::Property* prop)
{
    if (!propertyShape_) return;
    shapeProperties_.push_back(layout_property::fromOasis(*prop));
}

void OasisLayoutBuilder::addCellProperty(Oasis::Property* prop)
{
    cellProperties_.push_back(layout_property::fromOasis(*prop));
}

void OasisLayoutBuilder::addFileProperty(Oasis::Property* prop)
{
    fileProperties_.push_back(layout_property::fromOasis(*prop));
}
```

## 6. 요소가 끝나면 저장

기존 `endElement()` 안:
```cpp
if (propertyShape_ && !shapeProperties_.empty())
    layout_->properties.saveShape(propertyShape_, shapeProperties_);
propertyShape_ = nullptr;
shapeProperties_.clear();
```

기존 `endCell()` 안, currentCellId_ 초기화 전:
```cpp
layout_->properties.saveCell(currentCellId_, cellProperties_);
cellProperties_.clear();
```

기존 `endFile()` 안 (**파일 전체를 읽는 단일 Builder 기준**):
```cpp
layout_->properties.saveFile(fileProperties_);
fileProperties_.clear();
```

`beginCell()`에서는 cell 목록을 비우고, `beginFile()`에서는 file 목록을 비웁니다.
**TEXT/X 등 저장하지 않는 요소의 begin 콜백과 beginCell**에는 아래도 추가합니다:
```cpp
propertyShape_ = nullptr;
shapeProperties_.clear();
```

## 7. 전체 읽기가 끝나면 정렬

전체 Import가 완료되고 모든 worker가 join된 뒤, 성공 보고/결과 반환 전에:
```cpp
state->layout->properties.finish();
```

worker의 endCell/endFile에 넣으면 안 됩니다. 병렬 Import는 coordinator가 파일
Property를 수집해 saveFile을 한 번 호출하도록 별도 연결해야 합니다.

## 8. Shape의 Property 조회

```cpp
layout_property::PropertyList list = layout.properties.getShape(shape.base());
for (const layout_property::Property& property : list) {
    // property.name, property.values를 일반 구조체처럼 사용
}
```

Cell은 `getCell(cellId)`, File은 `getFile()`입니다. Repetition은 내부 base를
꺼내기 전 wrapper 주소로 조회합니다.

## GDSII 연결

GdsLayoutBuilder에도 현재 주소/목록 멤버를 추가하고 addShape/endElement에
같은 기억/저장 코드를 넣습니다. 기존 addProperty의 no-op을 다음으로 바꿉니다:
```cpp
if (propertyShape_)
    shapeProperties_.push_back(layout_property::fromGds(attr, std::string(value)));
```
생략하는 TEXT/NODE의 begin 콜백에서도 현재 주소/목록을 초기화합니다.
GDS의 기존 NUL-terminated 문자열 계약을 따르며 원본 padding 보존은 아닙니다.

## 적용 범위와 주의점

- 한 owner의 전체 목록을 한 번 저장합니다. finish 이후 추가/수정할 수 없습니다.
- 조회 결과는 복사본이므로 바꿔도 저장소가 바뀌지 않습니다.
- 저장소는 mutex 때문에 복사/이동 불가입니다. 기존 Layout 복사 계약을 확인하세요.
- Shape 주소는 Layout 수명 안에서만 사용합니다. 삭제/교체 시 연결 갱신은 미구현입니다.
- geometry 미수입 모드에서는 Shape Property도 미수입입니다. SDK에서 빈 목록 대신
  DataNotImported로 구분하는 연결이 필요합니다.
- name 레코드 자체의 Property, 생략 TEXT/X/NODE의 Property, SDK/Writer 연결은 미구현입니다.
- Multigon 전체 Import/roundtrip 검증은 하지 않았습니다. 이 파일은 붙여넣기 안내입니다.
