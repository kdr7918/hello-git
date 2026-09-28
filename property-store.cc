#include "property-store.h"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

namespace layout_property {

void Property::addText(const std::string& text, PropertyValue::Type type) {
    if (type != PropertyValue::Text && type != PropertyValue::Binary && type != PropertyValue::Name)
        throw std::invalid_argument("문자열 타입이 아닙니다");
    PropertyValue value;
    value.type = type;
    value.text = text;
    values.push_back(std::move(value));
}
void Property::addInteger(int64_t number) {
    PropertyValue value;
    value.type = PropertyValue::Integer;
    value.integer = number;
    values.push_back(std::move(value));
}
void Property::addUnsigned(uint64_t number) {
    PropertyValue value;
    value.type = PropertyValue::Unsigned;
    value.unsignedInteger = number;
    values.push_back(std::move(value));
}
void Property::addReal(double number) {
    PropertyValue value;
    value.type = PropertyValue::Double;
    value.real = number;
    values.push_back(std::move(value));
}
void Property::addFloat(float number) {
    PropertyValue value;
    value.type = PropertyValue::Float;
    value.real = number;
    values.push_back(std::move(value));
}
void Property::addRatio(int64_t numerator, int64_t denominator) {
    if (denominator <= 0) throw std::invalid_argument("분모는 양수여야 합니다");
    PropertyValue value;
    value.type = PropertyValue::Ratio;
    value.numerator = numerator;
    value.denominator = denominator;
    values.push_back(std::move(value));
}

void PropertyStore::checkSaving() const {
    if (finished_) throw std::logic_error("finish 이후에는 저장할 수 없습니다");
}
void PropertyStore::checkReading() const {
    if (!finished_) throw std::logic_error("finish 이후에 조회하세요");
}

// 목록을 그대로 복사합니다. blob, 이름 ID, encode/decode가 없습니다.
void PropertyStore::saveShape(const void* shape, const PropertyList& properties) {
    if (!shape) throw std::invalid_argument("Shape 주소가 없습니다");
    std::lock_guard<std::mutex> lock(mutex_);
    checkSaving();
    if (!properties.empty()) shapes_.push_back(ShapeEntry{shape, properties});
}
void PropertyStore::saveCell(uint32_t cellId, const PropertyList& properties) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkSaving();
    if (!properties.empty()) cells_.push_back(CellEntry{cellId, properties});
}
void PropertyStore::saveFile(const PropertyList& properties) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkSaving();
    if (fileSaved_) throw std::logic_error("파일 Property는 한 번만 저장하세요");
    PropertyList copy = properties;
    file_.swap(copy);
    fileSaved_ = true;
}

// 정렬은 여기서 한 번만. 같은 owner를 두 번 저장한 실수도 확인합니다.
void PropertyStore::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_) return;
    std::sort(shapes_.begin(), shapes_.end(), [](const ShapeEntry& a, const ShapeEntry& b) {
        return std::less<const void*>()(a.shape, b.shape);
    });
    std::sort(cells_.begin(), cells_.end(), [](const CellEntry& a, const CellEntry& b) {
        return a.cellId < b.cellId;
    });
    for (size_t i = 1; i < shapes_.size(); ++i)
        if (shapes_[i - 1].shape == shapes_[i].shape)
            throw std::logic_error("같은 Shape가 두 번 저장됐습니다");
    for (size_t i = 1; i < cells_.size(); ++i)
        if (cells_[i - 1].cellId == cells_[i].cellId)
            throw std::logic_error("같은 Cell이 두 번 저장됐습니다");
    finished_ = true;
}

PropertyList PropertyStore::getShape(const void* shape) const {
    checkReading();
    const auto it = std::lower_bound(shapes_.begin(), shapes_.end(), shape,
        [](const ShapeEntry& entry, const void* address) {
            return std::less<const void*>()(entry.shape, address);
        });
    if (it == shapes_.end() || it->shape != shape) return {};
    return it->properties;
}
PropertyList PropertyStore::getCell(uint32_t cellId) const {
    checkReading();
    const auto it = std::lower_bound(cells_.begin(), cells_.end(), cellId,
        [](const CellEntry& entry, uint32_t id) { return entry.cellId < id; });
    if (it == cells_.end() || it->cellId != cellId) return {};
    return it->properties;
}
PropertyList PropertyStore::getFile() const {
    checkReading();
    return file_;
}

} // namespace layout_property
