#ifndef SIMPLE_LAYOUT_PROPERTY_STORE_H
#define SIMPLE_LAYOUT_PROPERTY_STORE_H

#include "property.h"
#include <mutex>

namespace layout_property {

// Layout 안에 하나 둡니다. PropertyList를 인코딩 없이 그대로 소유합니다.
class PropertyStore {
public:
    void saveShape(const void* shape, const PropertyList& properties);
    void saveCell(uint32_t cellId, const PropertyList& properties);
    void saveFile(const PropertyList& properties);

    // 모든 저장이 끝난 뒤 한 번 호출. 주소/Cell ID 순으로 정렬합니다.
    void finish();

    // finish 이후에만 조회. 반환값은 복사본이며 없으면 빈 목록입니다.
    PropertyList getShape(const void* shape) const;
    PropertyList getCell(uint32_t cellId) const;
    PropertyList getFile() const;

private:
    struct ShapeEntry {
        const void* shape;
        PropertyList properties;
    };
    struct CellEntry {
        uint32_t cellId;
        PropertyList properties;
    };

    void checkSaving() const;
    void checkReading() const;

    std::vector<ShapeEntry> shapes_;
    std::vector<CellEntry> cells_;
    PropertyList file_;
    bool fileSaved_ = false;
    bool finished_ = false;
    std::mutex mutex_; // 병렬 저장 보호. finish는 worker join 이후 호출.
};

} // namespace layout_property
#endif
