#ifndef ASCII_RDB_HPP
#define ASCII_RDB_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace rdb {

/*
 * ASCII RDB 한 파일 전체를 담는, 메모리 사용량을 줄인 C++11 자료구조다.
 *
 * 일반적인 설계라면 Result마다 vector/string을 두기 쉽다. 하지만 결과가 수백만 개면
 * 작은 vector/string 객체 자체가 큰 메모리를 차지한다. 여기서는 가변 길이 데이터는
 * Database의 큰 배열 한 곳에 모으고, 각 레코드는 그 배열 안의 위치만 기억한다.
 *
 * - Result/RuleCheck 안에는 vector/string 대신 32비트 Range를 둔다.
 * - 모든 문자열 바이트는 StringTable 한 곳에 넣고 StringId로 참조한다.
 * - 좌표는 DBU가 큰 경우를 고려해 부호 있는 64비트 정수로 유지한다.
 *
 * 이 헤더는 자료구조만 제공하며 파서나 Qt에 의존하지 않는다.
 */

// StringTable 안의 문자열 번호와 전역 배열의 위치에 쓰는 타입이다.
// 32비트로 두어 Result/Range 구조체의 크기를 작게 유지한다.
typedef std::uint32_t StringId;
typedef std::uint32_t Index;
typedef std::uint64_t FileOffset;
typedef FileOffset CheckOffset;
typedef Index CheckId;

inline StringId invalid_string_id() {
    // 실제 문자열 ID로는 만들지 않는 가장 큰 값을 "없음" 표시로 사용한다.
    return std::numeric_limits<StringId>::max();
}

inline Index invalid_index() {
    return std::numeric_limits<Index>::max();
}

inline CheckId invalid_check_id() {
    return invalid_index();
}

// Database 안의 연속 배열 일부를 가리킨다.
// 예를 들어 {begin=10, count=3}은 10, 11, 12번째 원소를 뜻한다.
struct Range {
    Index begin;
    Index count;

    Range() : begin(0), count(0) {}
    Range(Index first, Index size) : begin(first), count(size) {}

    bool empty() const { return count == 0; }
    Index end() const { return begin + count; }
};

// StringTable 내부 바이트를 빌려 보는 뷰다. 메모리를 소유하지 않는다.
// 이후 StringTable에 문자열을 추가하면 vector 재할당으로 data 포인터가 바뀔 수 있다.
struct StringRef {
    const char* data;
    Index size;

    StringRef() : data(0), size(0) {}
    StringRef(const char* text, Index length) : data(text), size(length) {}

    bool empty() const { return size == 0; }
    std::string str() const { return data == 0 ? std::string() : std::string(data, size); }
};

struct StringRecord {
    // bytes_ 배열 안에서 한 문자열이 시작하는 위치와 길이만 저장한다.
    Index offset;
    Index size;

    StringRecord() : offset(0), size(0) {}
    StringRecord(Index first, Index length) : offset(first), size(length) {}
};

/*
 * 문자열 바이트를 하나의 bytes_ 배열에 빈틈없이 저장한다.
 *
 * StringId는 StringTable이 살아 있는 동안 안정적이다. 반면 StringRef는 bytes_의
 * 실제 주소를 빌리므로, 문자열을 더 추가한 뒤에는 get()으로 다시 얻어야 한다.
 * 파서는 "CN", "EL" 같은 반복 태그 ID를 미리 intern하여 중복 바이트도 줄인다.
 */
class StringTable {
public:
    class Checkpoint {
    public:
        Checkpoint(const Checkpoint&) = default;
    private:
        friend class StringTable;
        Checkpoint(const StringTable* owner,
                   std::size_t epoch,
                   std::size_t string_count,
                   std::size_t byte_count)
            : owner_(owner), epoch_(epoch), string_count_(string_count), byte_count_(byte_count) {}
        const StringTable* owner_;
        std::size_t epoch_;
        std::size_t string_count_;
        std::size_t byte_count_;
    };

    StringTable() : epoch_(0) {}

    void reserve(std::size_t string_count, std::size_t byte_count) {
        // 대략적인 크기를 알 때 미리 확보하면 vector의 재할당 횟수가 줄어든다.
        records_.reserve(string_count);
        bytes_.reserve(byte_count);
    }

    StringId add(const char* text, std::size_t length) {
        // 널 종료 문자열일 필요가 없다. 파일 버퍼의 일부 Span도 바로 저장할 수 있다.
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

    Checkpoint checkpoint() const {
        return Checkpoint(this, epoch_, records_.size(), bytes_.size());
    }

    void rollback(const Checkpoint& checkpoint) {
        if (checkpoint.owner_ != this || checkpoint.epoch_ != epoch_ ||
            checkpoint.string_count_ > records_.size() ||
            checkpoint.byte_count_ > bytes_.size()) {
            throw std::out_of_range("StringTable rollback checkpoint is invalid");
        }
        records_.resize(checkpoint.string_count_);
        bytes_.resize(checkpoint.byte_count_);
        ++epoch_;
    }

    void clear() { records_.clear(); bytes_.clear(); ++epoch_; }

private:
    static std::size_t max_index() {
        return static_cast<std::size_t>(std::numeric_limits<Index>::max());
    }

    std::vector<StringRecord> records_;
    std::vector<char> bytes_;
    std::size_t epoch_;
};

struct Point {
    // RDB 좌표 한 점. DBU 기준 정수이므로 float 오차가 없다.
    std::int64_t x;
    std::int64_t y;

    Point() : x(0), y(0) {}
    Point(std::int64_t x_value, std::int64_t y_value)
        : x(x_value), y(y_value) {}
};

struct Edge {
    // edge 결과 한 개는 두 끝점으로 표현한다.
    Point first;
    Point second;

    Edge() {}
    Edge(const Point& first_point, const Point& second_point)
        : first(first_point), second(second_point) {}
};

// 태그 한 줄을 ID와 payload로 나눈다. 두 문자열 모두 StringTable에 들어 있다.
// 예: "PP M1 marker"는 id="PP", payload="M1 marker"가 된다.
struct TaggedValue {
    StringId id;
    StringId payload;

    TaggedValue()
        : id(invalid_string_id()), payload(invalid_string_id()) {}
    TaggedValue(StringId id_value, StringId payload_value)
        : id(id_value), payload(payload_value) {}
};

enum class ResultKind : std::uint8_t {
    Polygon,     // "p <순번> <정점 수>"
    EdgeCluster  // "e <순번> <edge 수>"
};

// p/e 결과 하나다. property는 좌표 전/사이/후 위치를 구분하지 않고 파일에서 발견한
// 순서대로 하나의 연속 Range에 저장한다. geometry는 전역 좌표 배열 구간을 가리킨다.
// kind가 Polygon이면 Database::vertices, EdgeCluster이면 Database::edges를 사용한다.
struct Result {
    Range properties;
    Range geometry;
    StringId signature_suffix;
    std::uint32_t ordinal;
    ResultKind kind;

    Result()
        : signature_suffix(invalid_string_id()),
          ordinal(0),
          kind(ResultKind::Polygon) {}
};

// RuleCheck는 index 단계에서 name/comment/offset/count를 채우고, detail load 후 나머지
// Range와 실행 정보를 같은 객체에 완성한다. detail_loaded는 결과 0개인 loaded Check와
// 아직 load하지 않은 Check를 구분한다.
struct RuleCheck {
    CheckOffset offset;
    StringId name;
    StringId comment;
    StringId executed_at;
    std::uint32_t current_result_count;
    std::uint32_t original_result_count;
    std::uint32_t declared_check_text_count;
    Range check_text;
    Range results;
    bool detail_loaded;

    RuleCheck()
        : offset(0),
          name(invalid_string_id()),
          comment(invalid_string_id()),
          executed_at(invalid_string_id()),
          current_result_count(0),
          original_result_count(0),
          declared_check_text_count(0),
          detail_loaded(false) {}
};

/*
 * 파일 전체의 실제 저장소다. 아래 전역 배열에 데이터를 한 번씩만 넣는다.
 *
 * RuleCheck::check_text             -> check_text_lines
 * RuleCheck::results                -> results
 * Result::properties                -> tagged_values
 * Result::geometry                  -> kind에 따라 vertices 또는 edges
 */
struct Database {
    StringTable strings;
    StringId top_cell_name;
    double database_precision;

    std::vector<RuleCheck> rule_checks;
    std::vector<Result> results;
    std::vector<Point> vertices;
    std::vector<Edge> edges;
    std::vector<TaggedValue> tagged_values;
    std::vector<StringId> check_text_lines;
    std::size_t loaded_rule_check_count;

    Database()
        : top_cell_name(invalid_string_id()),
          database_precision(0.0),
          loaded_rule_check_count(0) {}

    bool has_valid_precision() const {
        return std::isfinite(database_precision) && database_precision > 0.0;
    }

    std::size_t check_count() const { return rule_checks.size(); }
    std::size_t loaded_check_count() const { return loaded_rule_check_count; }

    const RuleCheck& check(CheckId id) const {
        if (id == invalid_check_id() || static_cast<std::size_t>(id) >= rule_checks.size()) {
            throw std::out_of_range("RDB check ID is out of range");
        }
        return rule_checks[id];
    }

    RuleCheck& check(CheckId id) {
        if (id == invalid_check_id() || static_cast<std::size_t>(id) >= rule_checks.size()) {
            throw std::out_of_range("RDB check ID is out of range");
        }
        return rule_checks[id];
    }

    CheckId find_check_by_offset(CheckOffset offset) const {
        for (std::size_t i = 0; i < rule_checks.size(); ++i) {
            if (rule_checks[i].offset == offset) return static_cast<CheckId>(i);
        }
        return invalid_check_id();
    }

    std::vector<CheckId> find_checks(const std::string& name) const {
        std::vector<CheckId> found;
        for (std::size_t i = 0; i < rule_checks.size(); ++i) {
            const StringRef value = strings.get(rule_checks[i].name);
            if (value.size == name.size() &&
                (value.size == 0 || std::memcmp(value.data, name.data(), value.size) == 0)) {
                found.push_back(static_cast<CheckId>(i));
            }
        }
        return found;
    }

    void reserve(std::size_t rule_count,
                 std::size_t result_count,
                 std::size_t vertex_count,
                 std::size_t edge_count,
                 std::size_t tagged_value_count,
                 std::size_t check_text_line_count) {
        // 예상 개수를 아는 호출자는 이 함수로 대량 파싱 중 재할당을 줄일 수 있다.
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
