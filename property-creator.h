#ifndef SIMPLE_LAYOUT_PROPERTY_CREATOR_H
#define SIMPLE_LAYOUT_PROPERTY_CREATOR_H

#include "property.h"
#include "oasis/creator.h"
#include "oasis/names.h"
#include <climits>
#include <memory>
#include <stdexcept>

namespace layout_property {

// creator.endFile()이 반환할 때까지 보관하세요.
using OutputNames = std::vector<std::unique_ptr<Oasis::PropName>>;

enum class PropertyScope { File, Cell, Element };

// 공통 변환. 아래 writeFile/Cell/ElementProperties에서 호출합니다.
// 이름 객체만 OutputNames에 남고, 임시 Oasis::Property는 호출 후 파괴됩니다.
inline void writeProperties(Oasis::OasisCreator& creator,
                            const PropertyList& input,
                            OutputNames& names,
                            PropertyScope scope)
{
    using Value = PropertyValue;
    for (const auto& source : input) {
        if (source.isGds)
            throw std::invalid_argument("GDS 속성은 S_GDS_PROPERTY 변환이 필요합니다");

        names.emplace_back(new Oasis::PropName(source.name));
        Oasis::Property target(names.back().get(), source.standard);

        for (const auto& value : source.values) {
            switch (value.type) {
            case Value::Integer:
                target.addValue(Oasis::PV_SignedInteger,
                                static_cast<llong>(value.integer));
                break;
            case Value::Unsigned:
                target.addValue(Oasis::PV_UnsignedInteger,
                                static_cast<Ullong>(value.unsignedInteger));
                break;
            case Value::Ratio:
                if (value.numerator < -LONG_MAX || value.numerator > LONG_MAX ||
                    value.denominator <= 0 || value.denominator > LONG_MAX)
                    throw std::out_of_range("Oreal 유리수 범위 초과");
                // 실제 부호/표현은 Creator가 Oreal에서 판단합니다.
                target.addValue(Oasis::PV_Real_PosRatio,
                    Oasis::Oreal(static_cast<long>(value.numerator),
                                 static_cast<long>(value.denominator)));
                break;
            case Value::Float:
                target.addValue(Oasis::PV_Real_Float32,
                                Oasis::Oreal(static_cast<float>(value.real)));
                break;
            case Value::Double:
                target.addValue(Oasis::PV_Real_Float64, Oasis::Oreal(value.real));
                break;
            case Value::Text:
                target.addValue(Oasis::PV_AsciiString, value.text);
                break;
            case Value::Binary:
                target.addValue(Oasis::PV_BinaryString, value.text);
                break;
            case Value::Name:
                target.addValue(Oasis::PV_NameString, value.text);
                break;
            default:
                throw std::invalid_argument("알 수 없는 값 타입");
            }
        }
        switch (scope) {
        case PropertyScope::File: creator.addFileProperty(&target); break;
        case PropertyScope::Cell: creator.addCellProperty(&target); break;
        case PropertyScope::Element: creator.addElementProperty(&target); break;
        default: throw std::invalid_argument("알 수 없는 Property 저장 대상");
        }
    }
}

// beginFile() 직후, 첫 Cell 전에 호출합니다.
inline void writeFileProperties(Oasis::OasisCreator& creator,
                                const PropertyList& input, OutputNames& names)
{
    writeProperties(creator, input, names, PropertyScope::File);
}

// beginCell() 직후, 첫 도형 전에 호출합니다.
inline void writeCellProperties(Oasis::OasisCreator& creator,
                                const PropertyList& input, OutputNames& names)
{
    writeProperties(creator, input, names, PropertyScope::Cell);
}

// beginRectangle()/beginPlacement() 등 해당 요소 시작 직후 호출합니다.
inline void writeElementProperties(Oasis::OasisCreator& creator,
                                   const PropertyList& input, OutputNames& names)
{
    writeProperties(creator, input, names, PropertyScope::Element);
}

} // namespace layout_property
#endif
