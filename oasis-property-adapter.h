#ifndef SIMPLE_OASIS_PROPERTY_ADAPTER_H
#define SIMPLE_OASIS_PROPERTY_ADAPTER_H

#include "property.h"
#include "oasis/names.h" // Multigon 프로젝트에서 제공
#include <stdexcept>

namespace layout_property {

// OASIS parser의 Property 하나를 쉬운 Property 구조체로 복사합니다.
// Builder 콜백에서만 사용: 이름/문자열 참조가 해소된 상태여야 합니다.
inline Property fromOasis(const Oasis::Property& source) {
    if (!source.getName() || !source.getName()->hasName())
        throw std::invalid_argument("Property 이름이 해소되지 않았습니다");
    Property result;
    result.name = source.getName()->getName();
    result.standard = source.isStandard();

    for (size_t i = 0; i < source.numValues(); ++i) {
        const Oasis::PropValue& value = *source.getValue(i);
        if (value.isReal()) {
            const Oasis::Oreal& real = value.getRealValue();
            if (real.isRational()) result.addRatio(real.getNumerator(), real.getDenominator());
            else if (real.isFloatSingle()) result.addFloat(static_cast<float>(real.getValue()));
            else result.addReal(real.getValue());
        } else if (value.getType() == Oasis::PV_UnsignedInteger) {
            result.addUnsigned(value.getUnsignedIntegerValue());
        } else if (value.getType() == Oasis::PV_SignedInteger) {
            result.addInteger(value.getSignedIntegerValue());
        } else {
            const std::string* text;
            if (value.isReference()) {
                if (!value.isResolved() || !value.getPropStringValue() ||
                    !value.getPropStringValue()->hasName())
                    throw std::invalid_argument("Property 문자열이 해소되지 않았습니다");
                text = &value.getPropStringValue()->getName();
            } else if (value.isString()) {
                text = &value.getStringValue();
            } else {
                throw std::invalid_argument("지원하지 않는 Property 값입니다");
            }
            switch (value.getType()) {
                case Oasis::PV_AsciiString:
                case Oasis::PV_Ref_AsciiString: result.addText(*text); break;
                case Oasis::PV_BinaryString:
                case Oasis::PV_Ref_BinaryString: result.addText(*text, PropertyValue::Binary); break;
                case Oasis::PV_NameString:
                case Oasis::PV_Ref_NameString: result.addText(*text, PropertyValue::Name); break;
                default: throw std::invalid_argument("지원하지 않는 문자열 타입입니다");
            }
        }
    }
    return result;
}

inline Property fromGds(int attribute, const std::string& text) {
    if (attribute < 0) throw std::invalid_argument("GDS attribute는 음수일 수 없습니다");
    Property result;
    result.isGds = true;
    result.gdsAttribute = static_cast<uint32_t>(attribute);
    result.addText(text);
    return result;
}

} // namespace layout_property
#endif
