#include "property-store.h"
#include <cstring>
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

// 아래부터는 내부 변환입니다. 사용하는 쪽은 읽지 않아도 됩니다.
property_example::PropertyList PropertyStore::encode(const PropertyList& properties) {
    using namespace property_example;
    property_example::PropertyList encoded;
    for (const Property& property : properties) {
        if (property.isGds && property.standard)
            throw std::invalid_argument("GDS 속성에는 OASIS standard flag를 쓰지 않습니다");
        const uint64_t key = property.isGds ? property.gdsAttribute : storage_.internName(property.name);
        property_example::Property target(
            property.isGds ? KeyKind::GdsAttribute : KeyKind::OasisName, key, property.standard);
        for (const PropertyValue& value : property.values) {
            switch (value.type) {
                case PropertyValue::Integer: target.addSigned(value.integer); break;
                case PropertyValue::Unsigned: target.addUnsigned(value.unsignedInteger); break;
                case PropertyValue::Ratio: target.addRational(value.numerator, value.denominator); break;
                case PropertyValue::Float: target.addFloat(static_cast<float>(value.real)); break;
                case PropertyValue::Double: target.addDouble(value.real); break;
                case PropertyValue::Text: target.addString(ValueKind::AsciiString, value.text); break;
                case PropertyValue::Binary: target.addString(ValueKind::BinaryString, value.text); break;
                case PropertyValue::Name: target.addString(ValueKind::NameString, value.text); break;
                default: throw std::invalid_argument("알 수 없는 값 타입");
            }
        }
        encoded.append(target);
    }
    return encoded;
}

void PropertyStore::saveShape(const void* shape, const PropertyList& properties) {
    if (!shape) throw std::invalid_argument("Shape 주소가 없습니다");
    storage_.setElement(shape, encode(properties));
}
void PropertyStore::saveCell(uint32_t cellId, const PropertyList& properties) {
    storage_.setCell(cellId, encode(properties));
}
void PropertyStore::saveFile(const PropertyList& properties) {
    storage_.setFile(encode(properties));
}
void PropertyStore::finish() { storage_.freeze(); }
PropertyList PropertyStore::getShape(const void* shape) const { return decode(storage_.element(shape)); }
PropertyList PropertyStore::getCell(uint32_t cellId) const { return decode(storage_.cell(cellId)); }
PropertyList PropertyStore::getFile() const { return decode(storage_.file()); }

PropertyList PropertyStore::decode(property_example::PropertyListRef list) const {
    using namespace property_example;
    PropertyList result;
    storage_.forEach(list, [&](PropertyView stored) {
        Property property;
        property.isGds = stored.keyKind == KeyKind::GdsAttribute;
        property.standard = stored.standard;
        if (property.isGds) {
            if (stored.key > UINT32_MAX) throw std::out_of_range("GDS attribute 범위 초과");
            property.gdsAttribute = static_cast<uint32_t>(stored.key);
        } else {
            property.name = storage_.name(stored.key);
        }
        stored.forEachValue([&](ValueView value) {
            switch (value.kind) {
                case ValueKind::Signed: property.addInteger(signedWord(value.word())); break;
                case ValueKind::Unsigned: property.addUnsigned(value.word()); break;
                case ValueKind::Rational: {
                    const uint8_t* p = value.data;
                    const int64_t numerator = signedWord(get64(p, value.data + value.size));
                    const int64_t denominator = signedWord(get64(p, value.data + value.size));
                    property.addRatio(numerator, denominator);
                    break;
                }
                case ValueKind::Float32: {
                    uint32_t bits = 0;
                    for (unsigned i = 0; i < 4; ++i) bits |= uint32_t(value.data[i]) << (8 * i);
                    float number;
                    std::memcpy(&number, &bits, sizeof(number));
                    property.addFloat(number);
                    break;
                }
                case ValueKind::Float64: {
                    const uint64_t bits = value.word();
                    double number;
                    std::memcpy(&number, &bits, sizeof(number));
                    property.addReal(number);
                    break;
                }
                case ValueKind::AsciiString: property.addText(value.stringCopy()); break;
                case ValueKind::BinaryString: property.addText(value.stringCopy(), PropertyValue::Binary); break;
                case ValueKind::NameString: property.addText(value.stringCopy(), PropertyValue::Name); break;
            }
        });
        result.push_back(std::move(property));
    });
    return result;
}

} // namespace layout_property
