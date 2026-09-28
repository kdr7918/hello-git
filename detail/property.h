#ifndef PROPERTY_EXAMPLE_PROPERTY_H
#define PROPERTY_EXAMPLE_PROPERTY_H

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Newly written example, not copied from Multigon source.
namespace property_example {

using Bytes = std::vector<uint8_t>;
enum class KeyKind : uint8_t { OasisName, GdsAttribute };
enum class ValueKind : uint8_t {
    Unsigned, Signed, Rational, Float32, Float64,
    AsciiString, BinaryString, NameString
};

inline void put64(Bytes& dst, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        dst.push_back(static_cast<uint8_t>(value));
        value >>= 8;
    }
}
inline uint64_t get64(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 8) throw std::runtime_error("truncated property word");
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(*p++) << (8 * i);
    return value;
}
inline int64_t signedWord(uint64_t word) {
    // Do not rely on implementation-defined unsigned -> signed overflow.
    return word <= uint64_t(INT64_MAX) ? static_cast<int64_t>(word)
        : -1 - static_cast<int64_t>(UINT64_MAX - word);
}

// Temporary builder for ONE Property. No parser-owned pointers retained.
// key = PropertyStore::internName() result, or GDS PROPATTR number.
class Property {
public:
    Property(KeyKind kind, uint64_t key, bool standard = false)
        : kind_(kind), key_(key), standard_(standard), count_(0) {}

    void addUnsigned(uint64_t value) { addWord(ValueKind::Unsigned, value); }
    void addSigned(int64_t value) {
        addWord(ValueKind::Signed, static_cast<uint64_t>(value));
    }
    void addRational(int64_t numerator, int64_t denominator) {
        if (denominator <= 0) throw std::invalid_argument("denominator must be positive");
        Bytes data;
        put64(data, static_cast<uint64_t>(numerator));
        put64(data, static_cast<uint64_t>(denominator));
        add(ValueKind::Rational, data.data(), data.size());
    }
    void addFloat(float value) {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "IEEE binary32 required");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        uint8_t data[4];
        for (unsigned i = 0; i < 4; ++i) data[i] = uint8_t(bits >> (8 * i));
        add(ValueKind::Float32, data, sizeof(data));
    }
    void addDouble(double value) {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                      "IEEE binary64 required");
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        addWord(ValueKind::Float64, bits);
    }
    void addString(ValueKind kind, const std::string& value) {
        if (kind != ValueKind::AsciiString && kind != ValueKind::BinaryString &&
            kind != ValueKind::NameString)
            throw std::invalid_argument("not a string kind");
        add(kind, reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }

    KeyKind keyKind() const { return kind_; }
    uint64_t key() const { return key_; }
    bool isStandard() const { return standard_; }
    uint64_t valueCount() const { return count_; }
    const Bytes& encodedValues() const { return values_; }

private:
    void addWord(ValueKind kind, uint64_t value) {
        Bytes data;
        put64(data, value);
        add(kind, data.data(), data.size());
    }
    void add(ValueKind kind, const uint8_t* data, size_t size) {
        const size_t old = values_.size();
        try {
            if (count_ == UINT64_MAX) throw std::length_error("too many values");
            values_.push_back(static_cast<uint8_t>(kind));
            put64(values_, size);
            if (size) values_.insert(values_.end(), data, data + size);
            ++count_;
        } catch (...) { values_.resize(old); throw; }
    }
    KeyKind kind_;
    uint64_t key_;
    bool standard_;
    uint64_t count_;
    Bytes values_;
};

// Temporary buffer for ALL properties of one file/cell/element.
// Duplicate names, zero-value properties and source order are preserved.
struct PropertyList {
    Bytes bytes;
    uint64_t count = 0;

    void clear() { bytes.clear(); count = 0; }
    void append(const Property& property) {
        const size_t old = bytes.size();
        try {
            if (count == UINT64_MAX) throw std::length_error("too many properties");
            bytes.push_back(static_cast<uint8_t>(property.keyKind()));
            put64(bytes, property.key());
            bytes.push_back(property.isStandard() ? 1 : 0);
            put64(bytes, property.valueCount());
            put64(bytes, property.encodedValues().size());
            bytes.insert(bytes.end(), property.encodedValues().begin(),
                         property.encodedValues().end());
            ++count;
        } catch (...) { bytes.resize(old); throw; }
    }
};

// Non-owning views: valid only while their frozen PropertyStore lives.
struct ValueView {
    ValueKind kind;
    const uint8_t* data;
    size_t size;
    uint64_t word() const {
        if (size != 8) throw std::runtime_error("not an eight-byte value");
        const uint8_t* p = data;
        return get64(p, data + size);
    }
    std::string stringCopy() const {
        return std::string(reinterpret_cast<const char*>(data), size);
    }
};

struct PropertyView {
    KeyKind keyKind;
    uint64_t key;
    bool standard;
    uint64_t valueCount;
    const uint8_t* data;
    size_t size;

    template<class Visitor> void forEachValue(Visitor visit) const {
        const uint8_t* p = data;
        const uint8_t* end = data + size;
        for (uint64_t i = 0; i < valueCount; ++i) {
            if (p == end) throw std::runtime_error("missing value type");
            const auto kind = static_cast<ValueKind>(*p++);
            if (kind > ValueKind::NameString) throw std::runtime_error("bad value type");
            const uint64_t length = get64(p, end);
            if (length > uint64_t(end - p)) throw std::runtime_error("bad value length");
            const bool validSize = kind == ValueKind::Rational ? length == 16 :
                kind == ValueKind::Float32 ? length == 4 :
                (kind == ValueKind::Unsigned || kind == ValueKind::Signed ||
                 kind == ValueKind::Float64) ? length == 8 : true;
            if (!validSize) throw std::runtime_error("bad typed value size");
            visit(ValueView{kind, p, static_cast<size_t>(length)});
            p += static_cast<size_t>(length);
        }
        if (p != end) throw std::runtime_error("extra value bytes");
    }
};

} // namespace property_example
#endif
