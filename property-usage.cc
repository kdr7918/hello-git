#include "property-store.h"
#include <cassert>
#include <iostream>

using namespace property_example;

int main() {
    PropertyStore store;
    int shapeA = 0, shapeB = 0; // Replace with shape.base() in Multigon.
    const uint64_t netName = store.internName("net_name");
    assert(netName == store.internName("net_name"));

    PropertyList list;
    Property first(KeyKind::OasisName, netName);
    first.addString(ValueKind::AsciiString, "VDD");
    list.append(first);
    Property second(KeyKind::OasisName, netName); // duplicate name preserved
    second.addString(ValueKind::BinaryString, std::string("A\0B", 3));
    second.addSigned(INT64_MIN);
    second.addUnsigned(UINT64_MAX);
    second.addRational(-1, 3);
    second.addFloat(0.5f);
    second.addDouble(0.25);
    list.append(second);
    Property empty(KeyKind::OasisName, store.internName("empty"), true);
    list.append(empty); // zero values is still one Property

    store.setElement(&shapeA, list);
    store.setCell(7, list);
    store.setFile(list);
    list.clear(); // Store owns its own copy.
    store.freeze();

    assert(store.element(&shapeB).count == 0);
    assert(store.cell(7).count == 3);
    assert(store.cell(8).count == 0);
    assert(store.file().count == 3);
    unsigned properties = 0, values = 0;
    store.forEach(store.element(&shapeA), [&](PropertyView property) {
        ++properties;
        assert(store.name(property.key) == (properties == 3 ? "empty" : "net_name"));
        assert(property.standard == (properties == 3));
        assert(property.valueCount == (properties == 1 ? 1u : properties == 2 ? 6u : 0u));
        std::cout << store.name(property.key) << ": " << property.valueCount << " values\n";
        property.forEachValue([&](ValueView value) {
            ++values;
            if (value.kind == ValueKind::BinaryString)
                assert(value.stringCopy() == std::string("A\0B", 3));
            if (value.kind == ValueKind::Signed)
                assert(signedWord(value.word()) == INT64_MIN);
            if (value.kind == ValueKind::Unsigned)
                assert(value.word() == UINT64_MAX);
            if (value.kind == ValueKind::Rational) {
                const uint8_t* p = value.data;
                assert(signedWord(get64(p, value.data + value.size)) == -1);
                assert(get64(p, value.data + value.size) == 3);
            }
            if (value.kind == ValueKind::Float32) {
                uint32_t bits = 0;
                for (unsigned i = 0; i < 4; ++i) bits |= uint32_t(value.data[i]) << (8 * i);
                float result;
                std::memcpy(&result, &bits, sizeof(result));
                assert(result == 0.5f);
            }
            if (value.kind == ValueKind::Float64) {
                const uint64_t bits = value.word();
                double result;
                std::memcpy(&result, &bits, sizeof(result));
                assert(result == 0.25);
            }
        });
    });
    assert(properties == 3 && values == 7);
    bool refused = false;
    try { store.internName("late"); }
    catch (const std::logic_error&) { refused = true; }
    assert(refused);
    std::cout << "PASS: property ownership, lookup, order and typed values\n";
}
