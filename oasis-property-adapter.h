#ifndef PROPERTY_EXAMPLE_OASIS_ADAPTER_H
#define PROPERTY_EXAMPLE_OASIS_ADAPTER_H

#include "property-store.h"
#include "oasis/names.h" // supplied by the destination Multigon project

namespace property_example {

// Call only from semantic Builder callbacks, after reference resolution.
// Copies data; retains no Oasis::Property / PropName / PropString pointers.
inline void appendOasis(PropertyStore& store, PropertyList& out,
                        const Oasis::Property& source) {
    if (!source.getName() || !source.getName()->hasName())
        throw std::invalid_argument("unresolved property name");
    Property property(KeyKind::OasisName,
        store.internName(source.getName()->getName()), source.isStandard());

    for (size_t i = 0; i < source.numValues(); ++i) {
        const Oasis::PropValue& value = *source.getValue(i);
        // Inspect Oreal itself, not just PV_Real_*: parser may normalize it.
        if (value.isReal()) {
            const Oasis::Oreal& real = value.getRealValue();
            if (real.isRational())
                property.addRational(real.getNumerator(), real.getDenominator());
            else if (real.isFloatSingle())
                property.addFloat(static_cast<float>(real.getValue()));
            else
                property.addDouble(real.getValue());
        } else if (value.getType() == Oasis::PV_UnsignedInteger) {
            property.addUnsigned(value.getUnsignedIntegerValue());
        } else if (value.getType() == Oasis::PV_SignedInteger) {
            property.addSigned(value.getSignedIntegerValue());
        } else {
            const std::string* text = nullptr;
            if (value.isReference()) {
                if (!value.isResolved() || !value.getPropStringValue() ||
                    !value.getPropStringValue()->hasName())
                    throw std::invalid_argument("unresolved property string");
                text = &value.getPropStringValue()->getName();
            } else if (value.isString()) {
                text = &value.getStringValue();
            } else {
                throw std::invalid_argument("unsupported property value type");
            }
            ValueKind kind;
            switch (value.getType()) {
                case Oasis::PV_AsciiString:
                case Oasis::PV_Ref_AsciiString: kind = ValueKind::AsciiString; break;
                case Oasis::PV_BinaryString:
                case Oasis::PV_Ref_BinaryString: kind = ValueKind::BinaryString; break;
                case Oasis::PV_NameString:
                case Oasis::PV_Ref_NameString: kind = ValueKind::NameString; break;
                default: throw std::invalid_argument("bad property string type");
            }
            property.addString(kind, *text);
        }
    }
    // Commit only a fully converted Property. Interned but unused names may
    // remain after conversion failure; abort the enclosing import on error.
    out.append(property);
}

inline void appendGds(PropertyList& out, int attribute, const std::string& value) {
    if (attribute < 0) throw std::invalid_argument("negative GDS attribute");
    Property property(KeyKind::GdsAttribute, static_cast<uint64_t>(attribute));
    property.addString(ValueKind::AsciiString, value);
    out.append(property);
}

} // namespace property_example
#endif
