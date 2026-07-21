#include "ascii_rdb_parser.hpp"

#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

namespace rdb {
namespace {

struct LineView {
    // 현재 줄을 복사하지 않고 버퍼 내부 포인터와 파일 위치로 표현한다.
    const char* data;
    std::size_t size;
    FileOffset offset;
    FileOffset next_offset;
    std::uint64_t number;

    LineView()
        : data(0), size(0), offset(0), next_offset(0), number(0) {}
};

struct StoredLine {
    // 다음 RuleCheck 경계를 미리 읽었을 때, 다시 처리하기 위해 그 줄만 복사해 둔다.
    std::string text;
    FileOffset offset;
    FileOffset next_offset;
    std::uint64_t number;

    StoredLine() : offset(0), next_offset(0), number(0) {}
};

struct Span {
    // 널 종료 문자열이 아닌 파일 버퍼 일부를 [begin, end)로 표현한다.
    const char* begin;
    const char* end;

    Span() : begin(0), end(0) {}
    Span(const char* first, const char* last) : begin(first), end(last) {}

    std::size_t size() const {
        return begin == 0 ? 0 : static_cast<std::size_t>(end - begin);
    }

    bool empty() const { return begin == end; }
};

bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\v' || value == '\f';
}

Span trim(Span value) {
    // 원본 버퍼를 수정하지 않고 앞뒤 공백이 제외된 범위만 돌려준다.
    while (value.begin != value.end && is_space(*value.begin)) ++value.begin;
    while (value.begin != value.end && is_space(*(value.end - 1))) --value.end;
    return value;
}

Span content(const LineView& line) {
    // Windows CRLF 파일의 '\r'은 줄 내용에서 제외한다.
    const char* end = line.data + line.size;
    if (end != line.data && *(end - 1) == '\r') --end;
    return Span(line.data, end);
}

bool next_word(Span& input, Span& word) {
    // 문자열을 복사하지 않고 첫 단어를 꺼낸다. input은 다음 단어 앞으로 이동한다.
    input = trim(input);
    if (input.empty()) return false;
    const char* end = input.begin;
    while (end != input.end && !is_space(*end)) ++end;
    word = Span(input.begin, end);
    input.begin = end;
    return true;
}

bool parse_unsigned(Span value, std::uint64_t& result) {
    // std::strtoull은 널 종료 문자열을 기대하므로, 파일 버퍼를 바로 읽기 위해 직접 변환한다.
    if (value.empty()) return false;
    std::uint64_t parsed = 0;
    for (const char* it = value.begin; it != value.end; ++it) {
        if (*it < '0' || *it > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(*it - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    result = parsed;
    return true;
}

bool parse_signed(Span value, std::int64_t& result) {
    // 좌표 부호와 int64_t 최솟값(-2^63)까지 안전하게 처리한다.
    if (value.empty()) return false;
    bool negative = false;
    if (*value.begin == '+' || *value.begin == '-') {
        negative = *value.begin == '-';
        ++value.begin;
    }
    std::uint64_t magnitude = 0;
    if (!parse_unsigned(value, magnitude)) return false;
    const std::uint64_t positive_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t negative_limit = positive_limit + 1U;
    if ((!negative && magnitude > positive_limit) || (negative && magnitude > negative_limit)) return false;
    if (negative && magnitude == negative_limit) {
        result = std::numeric_limits<std::int64_t>::min();
    } else {
        result = static_cast<std::int64_t>(magnitude);
        if (negative) result = -result;
    }
    return true;
}

/*
 * p/e 좌표는 전체 파싱에서 가장 자주 실행되는 경로다. Span을 여러 번 trim하고
 * next_word()를 호출하는 대신 포인터를 직접 이동하며 정수 하나를 읽는다.
 */
bool parse_coordinate_integer(const char*& cursor, const char* end, std::int64_t& result) {
    while (cursor != end && is_space(*cursor)) ++cursor;
    if (cursor == end) return false;

    bool negative = false;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        ++cursor;
    }

    const char* const first_digit = cursor;
    const std::uint64_t positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = negative ? positive_limit + 1U : positive_limit;
    std::uint64_t magnitude = 0;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') {
        const std::uint64_t digit = static_cast<std::uint64_t>(*cursor - '0');
        if (magnitude > (limit - digit) / 10U) return false;
        magnitude = magnitude * 10U + digit;
        ++cursor;
    }
    if (cursor == first_digit) return false;

    if (negative && magnitude == positive_limit + 1U) {
        result = std::numeric_limits<std::int64_t>::min();
    } else {
        result = static_cast<std::int64_t>(magnitude);
        if (negative) result = -result;
    }
    return true;
}

bool only_space(const char* cursor, const char* end) {
    while (cursor != end && is_space(*cursor)) ++cursor;
    return cursor == end;
}

bool coordinate_candidate(Span text) {
    text = trim(text);
    if (text.empty()) return false;
    const char first = *text.begin;
    return (first >= '0' && first <= '9') || first == '+' || first == '-';
}

class BufferedLineReader {
public:
    BufferedLineReader(const std::string& path, const ParseOptions& options)
        : fd_(-1),
          buffer_(options.read_buffer_bytes),
          max_line_bytes_(options.max_line_bytes),
          position_(0),
          available_(0),
          buffer_offset_(0),
          next_read_offset_(0),
          line_number_(0),
          eof_(false) {
        if (buffer_.empty()) throw std::invalid_argument("RDB parse buffer must not be empty");
        if (max_line_bytes_ == 0) throw std::invalid_argument("RDB maximum line size must not be zero");

        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open RDB file '" + path + "': " + std::strerror(errno));
        }
#ifdef POSIX_FADV_SEQUENTIAL
        // 전체 파서는 항상 앞에서 뒤로 한 번 읽으므로 OS readahead를 돕는다.
        (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    }

    ~BufferedLineReader() {
        if (fd_ >= 0) ::close(fd_);
    }

    FileOffset position() const {
        if (position_ < available_) return buffer_offset_ + static_cast<FileOffset>(position_);
        return next_read_offset_;
    }

    bool next(LineView& line) {
        // 대부분의 줄은 buffer_ 안에 있으므로 메모리 할당 없이 memchr로 '\n'을 찾는다.
        overflow_.clear();
        bool has_overflow = false;
        const FileOffset line_offset = position();

        for (;;) {
            if (position_ == available_) {
                if (!refill()) {
                    if (!has_overflow) return false;
                    line.data = overflow_.data();
                    line.size = overflow_.size();
                    line.offset = line_offset;
                    line.next_offset = position();
                    line.number = ++line_number_;
                    return true;
                }
            }

            const char* begin = &buffer_[position_];
            const void* newline = std::memchr(begin, '\n', available_ - position_);
            if (newline != 0) {
                const char* end = static_cast<const char*>(newline);
                const std::size_t length = static_cast<std::size_t>(end - begin);
                position_ += length + 1U;
                if (!has_overflow) {
                    line.data = begin;
                    line.size = length;
                } else {
                    append_overflow(begin, length);
                    line.data = overflow_.data();
                    line.size = overflow_.size();
                }
                line.offset = line_offset;
                line.next_offset = position();
                line.number = ++line_number_;
                return true;
            }

            // 한 줄이 버퍼 경계를 넘으면 그 줄만 overflow_에 이어 붙인다.
            append_overflow(begin, available_ - position_);
            has_overflow = true;
            position_ = available_;
        }
    }

private:
    bool refill() {
        // read()가 EINTR로 끊긴 경우에는 재시도하고, EOF는 한 번만 기록한다.
        if (eof_) return false;
        buffer_offset_ = next_read_offset_;
        for (;;) {
            const ssize_t received = ::read(fd_, &buffer_[0], buffer_.size());
            if (received > 0) {
                available_ = static_cast<std::size_t>(received);
                position_ = 0;
                next_read_offset_ += static_cast<FileOffset>(received);
                return true;
            }
            if (received == 0) {
                eof_ = true;
                available_ = 0;
                position_ = 0;
                return false;
            }
            if (errno != EINTR) {
                throw std::runtime_error("cannot read RDB file: " + std::string(std::strerror(errno)));
            }
        }
    }

    void append_overflow(const char* text, std::size_t size) {
        // 잘못된 파일이 무한히 긴 한 줄을 만들지 못하게 상한을 검사한다.
        if (size > max_line_bytes_ || overflow_.size() > max_line_bytes_ - size) {
            throw std::length_error("RDB input line exceeds configured maximum length");
        }
        overflow_.append(text, size);
    }

    int fd_;
    std::vector<char> buffer_;
    std::size_t max_line_bytes_;
    std::size_t position_;
    std::size_t available_;
    FileOffset buffer_offset_;
    FileOffset next_read_offset_;
    std::uint64_t line_number_;
    bool eof_;
    std::string overflow_;
};

struct ResultSignature {
    // "p 순번 좌표수" 또는 "e 순번 좌표수" 줄을 해석한 결과다.
    ResultKind kind;
    std::uint64_t ordinal;
    std::uint64_t geometry_count;
    Span suffix;
};

struct RuleHeader {
    // "현재 결과 수 원래 결과 수 check text 줄 수 시간" 헤더의 분해 결과다.
    std::uint64_t current_result_count;
    std::uint64_t original_result_count;
    std::uint64_t check_text_line_count;
    Span executed_at;
};

bool parse_result_signature(Span text, ResultSignature& result) {
    Span word;
    if (!next_word(text, word) || word.size() != 1U || (*word.begin != 'p' && *word.begin != 'e')) return false;
    result.kind = *word.begin == 'p' ? ResultKind::Polygon : ResultKind::EdgeCluster;
    if (!next_word(text, word) || !parse_unsigned(word, result.ordinal)) return false;
    if (!next_word(text, word) || !parse_unsigned(word, result.geometry_count)) return false;
    result.suffix = trim(text);
    return true;
}

bool parse_rule_header(Span text, RuleHeader& result) {
    Span word;
    if (!next_word(text, word) || !parse_unsigned(word, result.current_result_count)) return false;
    if (!next_word(text, word) || !parse_unsigned(word, result.original_result_count)) return false;
    if (!next_word(text, word) || !parse_unsigned(word, result.check_text_line_count)) return false;
    result.executed_at = trim(text);
    return !result.executed_at.empty();
}

bool parse_point(Span text, Point& result) {
    // p 결과의 좌표 행은 정확히 x y 두 정수여야 한다.
    const char* cursor = text.begin;
    return parse_coordinate_integer(cursor, text.end, result.x) &&
           parse_coordinate_integer(cursor, text.end, result.y) &&
           only_space(cursor, text.end);
}

bool parse_edge(Span text, Edge& result) {
    // e 결과의 좌표 행은 x1 y1 x2 y2 네 정수여야 한다.
    const char* cursor = text.begin;
    return parse_coordinate_integer(cursor, text.end, result.first.x) &&
           parse_coordinate_integer(cursor, text.end, result.first.y) &&
           parse_coordinate_integer(cursor, text.end, result.second.x) &&
           parse_coordinate_integer(cursor, text.end, result.second.y) &&
           only_space(cursor, text.end);
}

StoredLine store(const LineView& line) {
    StoredLine result;
    result.text.assign(line.data, line.size);
    result.offset = line.offset;
    result.next_offset = line.next_offset;
    result.number = line.number;
    return result;
}

Index checked_index(std::size_t value, const char* description) {
    // 자료구조의 Range가 32비트이므로, 잘못된 wraparound 전에 오류를 낸다.
    if (value > static_cast<std::size_t>(std::numeric_limits<Index>::max())) {
        throw std::length_error(std::string("RDB ") + description + " exceeds 32-bit range capacity");
    }
    return static_cast<Index>(value);
}

std::uint32_t checked_count(std::uint64_t value, const LineView& line, const char* description) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw ParseError(line.offset, line.number, std::string(description) + " exceeds 32-bit model capacity");
    }
    return static_cast<std::uint32_t>(value);
}

template <typename Value>
void reserve_additional(std::vector<Value>& values,
                        std::uint64_t additional,
                        const LineView& line,
                        const char* description) {
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<Index>::max());
    if (additional > maximum || values.size() > maximum - static_cast<std::size_t>(additional)) {
        throw ParseError(line.offset, line.number, std::string(description) + " exceeds 32-bit model capacity");
    }

    const std::size_t required = values.size() + static_cast<std::size_t>(additional);
    if (required <= values.capacity()) return;

    // p/e 선언에 이미 필요한 좌표 수가 있으므로, 큰 결과 하나를 읽으며 여러 번 재할당하지 않는다.
    std::size_t capacity = values.capacity() < 1024U ? 1024U : values.capacity();
    while (capacity < required && capacity <= maximum / 2U) capacity *= 2U;
    if (capacity < required) capacity = required;
    values.reserve(capacity);
}

class PropertyIdInterner {
public:
    PropertyIdInterner() {
        two_character_ids_.reserve(16);
    }

    StringId intern(StringTable& strings, Span value) {
        // 일반 RDB 태그(PP, CN, EL 등)는 대부분 두 글자다.
        // 두 바이트를 정수 key로 쓰면 FNV 해시/문자열 비교 없이 바로 찾을 수 있다.
        if (value.size() == 2U) {
            const std::uint16_t key = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(static_cast<unsigned char>(value.begin[0])) << 8U) |
                static_cast<std::uint16_t>(static_cast<unsigned char>(value.begin[1])));
            std::unordered_map<std::uint16_t, StringId>::iterator found = two_character_ids_.find(key);
            if (found != two_character_ids_.end()) return found->second;

            const StringId id = strings.add(value.begin, value.size());
            two_character_ids_.insert(std::make_pair(key, id));
            return id;
        }

        // 길이가 다른 확장 태그는 기존의 해시 + 실제 문자열 비교 경로를 사용한다.
        const std::uint64_t hash = hash_span(value);
        const std::pair<std::unordered_multimap<std::uint64_t, StringId>::iterator,
                        std::unordered_multimap<std::uint64_t, StringId>::iterator> range = ids_.equal_range(hash);
        for (std::unordered_multimap<std::uint64_t, StringId>::iterator it = range.first; it != range.second; ++it) {
            const StringRef existing = strings.get(it->second);
            if (existing.size == value.size() &&
                (existing.size == 0 || std::memcmp(existing.data, value.begin, existing.size) == 0)) {
                return it->second;
            }
        }
        const StringId id = strings.add(value.begin, value.size());
        ids_.insert(std::make_pair(hash, id));
        return id;
    }

private:
    static std::uint64_t hash_span(Span value) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const char* it = value.begin; it != value.end; ++it) {
            hash ^= static_cast<unsigned char>(*it);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    std::unordered_map<std::uint16_t, StringId> two_character_ids_;
    std::unordered_multimap<std::uint64_t, StringId> ids_;
};

class Parser {
public:
    Parser(const std::string& path, const ParseOptions& options)
        : reader_(path, options), options_(options) {}

    Database run() {
        // 작은 초기 reserve는 일반 파일의 재할당을 줄이며, 실제 크기를 제한하지는 않는다.
        Database database;
        database.rule_checks.reserve(1024);
        database.strings.reserve(2048, 128U * 1024U);

        LineView top_cell_header;
        if (!next_nonblank(top_cell_header)) fail_at(0, 1, "empty RDB file");
        parse_database_header(top_cell_header, database);

        LineView rule_name;
        while (next_nonblank(rule_name)) {
            // 파일 끝까지 RuleCheck를 하나씩 읽어 같은 전역 Database에 추가한다.
            parse_rule(rule_name, database);
        }
        return database;
    }

private:
    bool next_line(LineView& line) {
        // pending_은 다음 경계를 미리 읽었을 때 사용한다. 없으면 실제 파일에서 읽는다.
        if (pending_.empty()) return reader_.next(line);
        active_ = pending_.front();
        pending_.pop_front();
        line.data = active_.text.data();
        line.size = active_.text.size();
        line.offset = active_.offset;
        line.next_offset = active_.next_offset;
        line.number = active_.number;
        return true;
    }

    bool next_nonblank(LineView& line) {
        while (next_line(line)) {
            if (!trim(content(line)).empty()) return true;
        }
        return false;
    }

    void push_front(const StoredLine& line) {
        pending_.push_front(line);
    }

    void push_rule_boundary(const StoredLine& rule_name, const StoredLine& rule_header) {
        // deque 앞에 header를 먼저 넣어야, 다음 pop 시 name -> header 순서가 된다.
        pending_.push_front(rule_header);
        pending_.push_front(rule_name);
    }

    StringId add_text(Database& database, Span text) {
        return database.strings.add(text.begin, text.size());
    }

    void parse_database_header(const LineView& line, Database& database) {
        // 첫 줄의 마지막 단어를 DBU로 보고, 그 앞 전체를 Top cell 이름으로 보관한다.
        const Span text = trim(content(line));
        Span cursor = text;
        Span word;
        Span last_word;
        while (next_word(cursor, word)) last_word = word;

        std::int64_t precision = 0;
        if (last_word.empty() || !parse_signed(last_word, precision) || precision <= 0 ||
            last_word.begin == text.begin) {
            fail(line, "expected '<top cell name> <database precision>'");
        }

        const Span top_cell = trim(Span(text.begin, last_word.begin));
        if (top_cell.empty()) fail(line, "empty top-cell name");
        database.top_cell_name = add_text(database, top_cell);
        database.database_precision = precision;
    }

    void parse_rule(const LineView& name_line, Database& database) {
        // RuleCheck는 이름 줄, 헤더, check text, p/e 결과 순으로 구성된다.
        const Span name = trim(content(name_line));
        if (name.empty()) fail(name_line, "empty rule-check name");

        LineView header_line;
        if (!next_line(header_line)) fail_at(name_line.next_offset, name_line.number + 1U, "truncated rule-check header");
        RuleHeader header;
        if (!parse_rule_header(trim(content(header_line)), header)) {
            fail(header_line, "expected '<current> <original> <check-text count> <date/time>'");
        }

        RuleCheck rule;
        rule.name = add_text(database, name);
        rule.executed_at = add_text(database, header.executed_at);
        rule.current_result_count = checked_count(header.current_result_count, header_line, "current result count");
        rule.original_result_count = checked_count(header.original_result_count, header_line, "original result count");

        // 헤더에 text/result 개수가 있으므로 전역 배열의 재할당을 미리 줄인다.
        reserve_additional(database.check_text_lines, header.check_text_line_count, header_line, "check-text count");
        const Index text_begin = checked_index(database.check_text_lines.size(), "check-text begin");
        for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
            LineView text_line;
            if (!next_line(text_line)) fail_at(reader_.position(), header_line.number + i + 1U, "truncated check text");
            database.check_text_lines.push_back(add_text(database, content(text_line)));
        }
        rule.check_text = Range(text_begin,
                                checked_index(database.check_text_lines.size() - text_begin, "check-text count"));

        reserve_additional(database.results, rule.current_result_count, header_line, "result count");
        const Index result_begin = checked_index(database.results.size(), "result begin");
        for (std::uint32_t i = 0; i < rule.current_result_count; ++i) {
            // Result는 전역 배열에 좌표/태그를 채운 뒤, 마지막에 결과 레코드 자체를 넣는다.
            Result result = parse_result(database);
            if (i + 1U == rule.current_result_count) {
                // 마지막 결과 뒤에는 다음 RuleCheck 또는 EOF가 올 수 있다.
                consume_final_result_tail(database, result);
            } else {
                consume_nonfinal_result_tail(database, result);
            }
            database.results.push_back(result);
        }
        rule.results = Range(result_begin,
                             checked_index(database.results.size() - result_begin, "result count"));

        if (rule.current_result_count == 0) {
            // 결과가 없는 RuleCheck도 다음 RuleCheck 경계를 확인해야 한다.
            consume_empty_rule_boundary();
        }
        database.rule_checks.push_back(rule);
    }

    Result parse_result(Database& database) {
        // p/e 선언 한 개와 그 앞쪽 태그, 좌표 블록을 읽는다.
        LineView signature_line;
        if (!next_nonblank(signature_line)) fail_at(reader_.position(), 0, "truncated result list");
        ResultSignature signature;
        if (!parse_result_signature(trim(content(signature_line)), signature)) {
            fail(signature_line, "expected 'p <ordinal> <vertex count>' or 'e <ordinal> <edge count>'");
        }

        Result result;
        result.kind = signature.kind;
        result.ordinal = checked_count(signature.ordinal, signature_line, "result ordinal");
        result.signature_suffix = signature.suffix.empty() ? invalid_string_id() : add_text(database, signature.suffix);

        const Index before_begin = checked_index(database.tagged_values.size(), "property begin");
        const Index geometry_begin = signature.kind == ResultKind::Polygon
            ? checked_index(database.vertices.size(), "vertex begin")
            : checked_index(database.edges.size(), "edge begin");

        if (signature.kind == ResultKind::Polygon) {
            reserve_additional(database.vertices, signature.geometry_count, signature_line, "vertex count");
        } else {
            reserve_additional(database.edges, signature.geometry_count, signature_line, "edge count");
        }

        std::uint64_t coordinates_seen = 0;
        while (coordinates_seen < signature.geometry_count) {
            // 좌표 사이에 태그가 끼어도 허용한다. 실제 좌표 수가 선언 값에 도달해야 끝난다.
            LineView line;
            if (!next_line(line)) fail_at(reader_.position(), signature_line.number, "truncated result geometry");
            const Span raw_text = content(line);
            const Span text = trim(raw_text);
            if (text.empty()) continue;
            const bool is_coordinate = coordinate_candidate(text);

            if (signature.kind == ResultKind::Polygon) {
                Point point;
                if (is_coordinate && parse_point(raw_text, point)) {
                    database.vertices.push_back(point);
                    ++coordinates_seen;
                    continue;
                }
            } else {
                Edge edge;
                if (is_coordinate && parse_edge(raw_text, edge)) {
                    database.edges.push_back(edge);
                    ++coordinates_seen;
                    continue;
                }
            }

            ResultSignature unexpected;
            if ((*text.begin == 'p' || *text.begin == 'e') && parse_result_signature(text, unexpected)) {
                fail(line, "result ended before its declared coordinate count");
            }
            append_property(database, text);
        }

        result.properties_before_geometry = Range(
            before_begin, checked_index(database.tagged_values.size() - before_begin, "property count"));
        result.geometry = Range(
            geometry_begin,
            checked_index(signature.kind == ResultKind::Polygon
                              ? database.vertices.size() - geometry_begin
                              : database.edges.size() - geometry_begin,
                          "geometry count"));

        return result;
    }

    void consume_nonfinal_result_tail(Database& database, Result& result) {
        // 다음 p/e 선언 전까지의 태그는 현재 Result의 "좌표 뒤 태그"로 분류한다.
        const Index after_begin = checked_index(database.tagged_values.size(), "post-property begin");
        for (;;) {
            LineView line;
            if (!next_nonblank(line)) fail_at(reader_.position(), 0, "result count exceeds physical result list");
            ResultSignature next_result;
            if (parse_result_signature(trim(content(line)), next_result)) {
                // 다음 결과의 선언을 미리 읽었으므로 pending_에 되돌려 다음 호출이 처리하게 한다.
                push_front(store(line));
                result.properties_after_geometry = Range(
                    after_begin, checked_index(database.tagged_values.size() - after_begin, "post-property count"));
                return;
            }
            if (!options_.allow_properties_after_geometry) {
                fail(line, "unexpected line after result geometry");
            }
            append_property(database, trim(content(line)));
        }
    }

    void consume_final_result_tail(Database& database, Result& result) {
        // 마지막 결과는 다음 Result가 아닌 다음 RuleCheck를 만나야 경계가 끝난다.
        const Index after_begin = checked_index(database.tagged_values.size(), "post-property begin");
        for (;;) {
            LineView candidate_line;
            if (!next_nonblank(candidate_line)) {
                result.properties_after_geometry = Range(
                    after_begin, checked_index(database.tagged_values.size() - after_begin, "post-property count"));
                return;
            }

            ResultSignature extra_result;
            if (parse_result_signature(trim(content(candidate_line)), extra_result)) {
                fail(candidate_line, "physical result list exceeds result count in rule header");
            }

            const StoredLine candidate = store(candidate_line);
            LineView possible_header_line;
            if (!next_line(possible_header_line)) {
                if (!options_.allow_properties_after_geometry) {
                    fail(candidate_line, "unexpected line after final result geometry");
                }
                append_property(database, trim(content(candidate_line)));
                result.properties_after_geometry = Range(
                    after_begin, checked_index(database.tagged_values.size() - after_begin, "post-property count"));
                return;
            }

            RuleHeader possible_header;
            if (parse_rule_header(trim(content(possible_header_line)), possible_header)) {
                // 후보 이름 + 헤더 조합이 확인되면 다음 RuleCheck용으로 두 줄을 되돌린다.
                push_rule_boundary(candidate, store(possible_header_line));
                result.properties_after_geometry = Range(
                    after_begin, checked_index(database.tagged_values.size() - after_begin, "post-property count"));
                return;
            }

            if (!options_.allow_properties_after_geometry) {
                fail(candidate_line, "expected next rule-check header");
            }

            append_property(database, trim(content(candidate_line)));
            push_front(store(possible_header_line));
        }
    }

    void consume_empty_rule_boundary() {
        // 결과가 0개인 경우에도 다음 이름/헤더를 미리 확인해 파일 구조 오류를 잡는다.
        LineView name_line;
        if (!next_nonblank(name_line)) return;
        const StoredLine name = store(name_line);
        LineView header_line;
        if (!next_line(header_line)) fail(name_line, "truncated rule-check header after empty rule");
        RuleHeader header;
        if (!parse_rule_header(trim(content(header_line)), header)) {
            fail(header_line, "expected rule-check header after empty rule");
        }
        push_rule_boundary(name, store(header_line));
    }

    void append_property(Database& database, Span text) {
        // 첫 단어는 태그 ID, 나머지는 payload다. ID만 intern해 중복 저장을 줄인다.
        Span cursor = trim(text);
        Span id;
        if (!next_word(cursor, id)) return;
        const Span payload = trim(cursor);
        const StringId id_value = property_ids_.intern(database.strings, id);
        const StringId payload_value = payload.empty() ? invalid_string_id() : add_text(database, payload);
        database.tagged_values.push_back(TaggedValue(id_value, payload_value));
    }

    void fail(const LineView& line, const std::string& message) const {
        throw ParseError(line.offset, line.number, message);
    }

    void fail_at(FileOffset offset, std::uint64_t line, const std::string& message) const {
        throw ParseError(offset, line, message);
    }

    BufferedLineReader reader_;
    ParseOptions options_;
    PropertyIdInterner property_ids_;
    std::deque<StoredLine> pending_;
    StoredLine active_;
};

} // namespace

ParseError::ParseError(FileOffset offset, std::uint64_t line, const std::string& message)
    : std::runtime_error("RDB parse error at byte " + std::to_string(offset) +
                         ", line " + std::to_string(line) + ": " + message),
      offset_(offset),
      line_(line) {}

Database AsciiRdbParser::parse_file(const std::string& path, const ParseOptions& options) const {
    return Parser(path, options).run();
}

} // namespace rdb
