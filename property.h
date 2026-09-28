#ifndef SIMPLE_LAYOUT_PROPERTY_H
#define SIMPLE_LAYOUT_PROPERTY_H

#include <cstdint>
#include <string>
#include <vector>

namespace layout_property {

// 값 하나. type에 해당하는 필드만 사용합니다.
// 이 구조체는 입력/조회용이며 Shape마다 영구 저장하지 않습니다.
struct PropertyValue {
    enum Type { Integer, Unsigned, Ratio, Float, Double, Text, Binary, Name };

    Type type = Text;
    int64_t integer = 0;
    uint64_t unsignedInteger = 0;
    int64_t numerator = 0;
    int64_t denominator = 1;
    double real = 0;
    std::string text;
};

// 속성 하나: 이름 + 값 목록. 같은 이름의 속성도 여러 개 허용합니다.
struct Property {
    std::string name;                  // OASIS 이름. 예: "net_name"
    bool standard = false;            // OASIS 표준 속성 여부
    bool isGds = false;
    uint32_t gdsAttribute = 0;         // GDS이면 이름 대신 이 번호 사용
    std::vector<PropertyValue> values;

    void addText(const std::string& text,
                 PropertyValue::Type type = PropertyValue::Text);
    void addInteger(int64_t number);
    void addUnsigned(uint64_t number);
    void addReal(double number);
    void addFloat(float number);
    void addRatio(int64_t numerator, int64_t denominator);
};

// 한 Shape / Cell / File에 붙는 속성 목록.
using PropertyList = std::vector<Property>;

} // namespace layout_property
#endif
