#ifndef ASCII_RDB_HPP
#define ASCII_RDB_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace rdb {

/*
 * Compact C++11 data model for an ASCII DRC Results Database (RDB).
 *
 * Design rules:
 * - No per-result std::vector or std::string allocation.
 * - All variable-size payloads live in contiguous database-wide arrays.
 * - Result/RuleCheck records refer to those arrays through 32-bit ranges.
 * - Text is stored once in StringTable and referred to by 32-bit StringId.
 *
 * The model has no parser and no Qt dependency.  Coordinates remain signed
 * 64-bit integers, even though many databases fit in 32 bits.
 */

typedef std::uint32_t StringId;
typedef std::uint32_t Index;

inline StringId invalid_string_id() {
    return std::numeric_limits<StringId>::max();
}

inline Index invalid_index() {
    return std::numeric_limits<Index>::max();
}

/* A compact reference to a contiguous slice in one database-wide array. */
struct Range {
    Index begin;
    Index count;

    Range() : begin(0), count(0) {}
    Range(Index first, Index size) : begin(first), count(size) {}

    bool empty() const { return count == 0; }
    Index end() const { return begin + count; }
};

/* A non-owning view returned by StringTable::get(). */
struct StringRef {
    const char* data;
    Index size;

    StringRef() : data(0), size(0) {}
    StringRef(const char* text, Index length) : data(text), size(length) {}

    bool empty() const { return size == 0; }
    std::string str() const { return data == 0 ? std::string() : std::string(data, size); }
};

struct StringRecord {
    Index offset;
    Index size;

    StringRecord() : offset(0), size(0) {}
    StringRecord(Index first, Index length) : offset(first), size(length) {}
};

/*
 * Stores text bytes densely.  A StringId is stable while the StringTable
 * exists; a StringRef is transient and must be requested again after adding
 * strings, because vector reallocation can move the backing bytes.
 *
 * The parser can intern repeated text (property IDs such as "CN" or "EL",
 * rule names, etc.) before calling add() to obtain additional deduplication.
 */
class StringTable {
public:
    StringTable() {}

    void reserve(std::size_t string_count, std::size_t byte_count) {
        records_.reserve(string_count);
        bytes_.reserve(byte_count);
    }

    StringId add(const char* text, std::size_t length) {
        if (length != 0 && text == 0) {
            throw std::invalid_argument("StringTable::add received null text");
        }
        if (length > max_index() || bytes_.size() > max_index() - length ||
            records_.size() >= max_index()) {
            throw std::length_error("RDB string table exceeds 32-bit capacity");
        }

        const Index offset = static_cast<Index>(bytes_.size());
        if (length != 0) bytes_.insert(bytes_.end(), text, text + length);
        records_.push_back(StringRecord(offset, static_cast<Index>(length)));
        return static_cast<StringId>(records_.size() - 1);
    }

    StringId add(const std::string& text) {
        return add(text.data(), text.size());
    }

    bool contains(StringId id) const {
        return id != invalid_string_id() && id < records_.size();
    }

    StringRef get(StringId id) const {
        if (!contains(id)) return StringRef();
        const StringRecord& record = records_[id];
        const char* first = record.size == 0 ? 0 : &bytes_[record.offset];
        return StringRef(first, record.size);
    }

    std::size_t size() const { return records_.size(); }
    std::size_t byte_size() const { return bytes_.size(); }
    void clear() { records_.clear(); bytes_.clear(); }

private:
    static std::size_t max_index() {
        return static_cast<std::size_t>(std::numeric_limits<Index>::max());
    }

    std::vector<StringRecord> records_;
    std::vector<char> bytes_;
};

struct Point {
    std::int64_t x;
    std::int64_t y;

    Point() : x(0), y(0) {}
    Point(std::int64_t x_value, std::int64_t y_value)
        : x(x_value), y(y_value) {}
};

struct Edge {
    Point first;
    Point second;

    Edge() {}
    Edge(const Point& first_point, const Point& second_point)
        : first(first_point), second(second_point) {}
};

/*
 * Property ID + operation-dependent payload, both stored in StringTable.
 * Examples of ID are CN, PP, PA, EL, EW, DA, and DV.
 */
struct TaggedValue {
    StringId id;
    StringId payload;

    TaggedValue()
        : id(invalid_string_id()), payload(invalid_string_id()) {}
    TaggedValue(StringId id_value, StringId payload_value)
        : id(id_value), payload(payload_value) {}
};

enum class ResultKind : std::uint8_t {
    Polygon,     // "p <ordinal> <vertex-count>"
    EdgeCluster  // "e <ordinal> <edge-count>"
};

/*
 * One p/e record.  geometry points into Database::vertices for Polygon or
 * Database::edges for EdgeCluster.  geometry.count is the p/e declared count.
 */
struct Result {
    Range properties_before_geometry;
    Range geometry;
    Range properties_after_geometry;
    StringId signature_suffix;
    std::uint32_t ordinal;
    ResultKind kind;

    Result()
        : signature_suffix(invalid_string_id()),
          ordinal(0),
          kind(ResultKind::Polygon) {}
};

/* One rule-check block in the RDB. */
struct RuleCheck {
    StringId name;
    StringId executed_at;
    std::uint32_t current_result_count;
    std::uint32_t original_result_count;
    Range check_text;
    Range results;

    RuleCheck()
        : name(invalid_string_id()),
          executed_at(invalid_string_id()),
          current_result_count(0),
          original_result_count(0) {}
};

/*
 * Whole-file storage.  Range fields in RuleCheck and Result index these arrays:
 *
 * RuleCheck::check_text                 -> check_text_lines
 * RuleCheck::results                    -> results
 * Result::properties_before/after_*     -> tagged_values
 * Result::geometry                      -> vertices or edges, by ResultKind
 */
struct Database {
    StringTable strings;
    StringId top_cell_name;
    std::int64_t database_precision;

    std::vector<RuleCheck> rule_checks;
    std::vector<Result> results;
    std::vector<Point> vertices;
    std::vector<Edge> edges;
    std::vector<TaggedValue> tagged_values;
    std::vector<StringId> check_text_lines;

    Database()
        : top_cell_name(invalid_string_id()),
          database_precision(0) {}

    bool has_valid_precision() const {
        return database_precision > 0;
    }

    void reserve(std::size_t rule_count,
                 std::size_t result_count,
                 std::size_t vertex_count,
                 std::size_t edge_count,
                 std::size_t tagged_value_count,
                 std::size_t check_text_line_count) {
        rule_checks.reserve(rule_count);
        results.reserve(result_count);
        vertices.reserve(vertex_count);
        edges.reserve(edge_count);
        tagged_values.reserve(tagged_value_count);
        check_text_lines.reserve(check_text_line_count);
    }
};

} // namespace rdb

#endif // ASCII_RDB_HPP
