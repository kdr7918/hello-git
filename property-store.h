#ifndef SIMPLE_LAYOUT_PROPERTY_STORE_H
#define SIMPLE_LAYOUT_PROPERTY_STORE_H

#include "property.h"
#include "detail/property-store.h"

namespace layout_property {

// Layout 안에 하나 둡니다. Shape 구조체는 바꾸지 않습니다.
class PropertyStore {
public:
    // 입력 목록을 복사해 내부 bytes에 저장합니다. 각 owner는 한 번만 저장.
    void saveShape(const void* shape, const PropertyList& properties);
    void saveCell(uint32_t cellId, const PropertyList& properties);
    void saveFile(const PropertyList& properties);

    // 모든 입력이 끝난 뒤 한 번 호출합니다. 이후 저장/수정 불가.
    void finish();

    // finish 이후 조회. 없으면 빈 목록. 반환값은 독립적인 복사본입니다.
    PropertyList getShape(const void* shape) const;
    PropertyList getCell(uint32_t cellId) const;
    PropertyList getFile() const;

private:
    // names/bytes/정렬 주소표는 내부에 숨깁니다.
    property_example::PropertyStore storage_;
    property_example::PropertyList encode(const PropertyList& properties);
    PropertyList decode(property_example::PropertyListRef list) const;
};

} // namespace layout_property
#endif
