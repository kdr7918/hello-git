#ifndef RDB_CHECK_INDEX_HPP
#define RDB_CHECK_INDEX_HPP

#include "ascii_rdb.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace rdb {

// 파일 시작 위치를 0으로 하는 바이트 단위 위치다.
// 인덱싱 결과의 offset을 나중에 CheckDetailParser에 그대로 전달한다.
typedef std::uint64_t CheckOffset;

// 가벼운 스캐너가 형식 오류를 발견했을 때 사용한다.
// 어느 바이트에서 문제가 났는지 함께 보관하므로 UI에서 오류 위치를 보여줄 수 있다.
class ScanError : public std::runtime_error {
public:
    ScanError(CheckOffset offset, const std::string& message)
        : std::runtime_error("RDB scan error at byte " + std::to_string(offset) + ": " + message),
          offset_(offset) {}

    CheckOffset offset() const { return offset_; }

private:
    CheckOffset offset_;
};

// TreeView의 한 행에 바로 사용할 수 있는 최소 정보다.
// geometry_count는 좌표 점 개수가 아니라 p/e 결과(결함 도형) 레코드 개수다.
struct CheckIndexEntry {
    std::string name;
    CheckOffset offset;
    std::uint32_t geometry_count;

    CheckIndexEntry() : offset(0), geometry_count(0) {}
};

namespace detail {

// 파일 버퍼를 복사하지 않고 [begin, end) 포인터 두 개로 표현한 문자열 조각이다.
// 대형 파일에서 std::string을 매 줄마다 만들지 않기 위해 사용한다.
struct Span {
    const char* begin;
    const char* end;

    Span() : begin(0), end(0) {}
    Span(const char* first, const char* last) : begin(first), end(last) {}

    bool empty() const { return begin == end; }
    std::size_t size() const { return static_cast<std::size_t>(end - begin); }
};

inline bool space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\v' || value == '\f';
}

inline Span trim(Span value) {
    // 앞뒤 공백만 건너뛴 새 Span을 반환한다. 원본 파일 데이터는 수정하지 않는다.
    while (value.begin != value.end && space(*value.begin)) ++value.begin;
    while (value.begin != value.end && space(*(value.end - 1))) --value.end;
    return value;
}

inline bool next_word(Span& input, Span& word) {
    // input의 첫 단어를 word에 돌려주고, input은 다음 단어 앞을 가리키게 한다.
    input = trim(input);
    if (input.empty()) return false;
    const char* end = input.begin;
    while (end != input.end && !space(*end)) ++end;
    word = Span(input.begin, end);
    input.begin = end;
    return true;
}

inline bool parse_unsigned(Span value, std::uint64_t& result) {
    // std::strtoull 대신 직접 숫자를 읽는다. 널 종료 문자열이나 임시 복사가 필요 없다.
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

inline bool parse_signed(Span value, std::int64_t& result) {
    // 좌표는 음수가 될 수 있으므로 부호와 int64_t 범위를 명시적으로 검사한다.
    if (value.empty()) return false;
    bool negative = false;
    if (*value.begin == '+' || *value.begin == '-') {
        negative = *value.begin == '-';
        ++value.begin;
    }
    std::uint64_t magnitude = 0;
    if (!parse_unsigned(value, magnitude)) return false;

    const std::uint64_t maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if ((!negative && magnitude > maximum) || (negative && magnitude > maximum + 1U)) return false;
    if (negative && magnitude == maximum + 1U) {
        result = std::numeric_limits<std::int64_t>::min();
    } else {
        result = static_cast<std::int64_t>(magnitude);
        if (negative) result = -result;
    }
    return true;
}

struct Line {
    Span text;
    CheckOffset offset;
};

// OS의 mmap으로 파일 내용을 가상 메모리에 연결한다.
// 실제 페이지는 접근할 때만 읽히므로 수십 GB 파일도 한 번에 복사하지 않는다.
class MappedFile {
public:
    explicit MappedFile(const std::string& path) : fd_(-1), data_(0), size_(0) {
        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open RDB file '" + path + "': " + std::strerror(errno));
        }

        struct stat status;
        if (::fstat(fd_, &status) != 0) {
            const std::string reason = std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("cannot stat RDB file '" + path + "': " + reason);
        }
        if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) >
                                  static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
            ::close(fd_);
            fd_ = -1;
            throw std::length_error("RDB file cannot be mapped in this process");
        }

        size_ = static_cast<std::size_t>(status.st_size);
        if (size_ == 0) return;

        void* mapping = ::mmap(0, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (mapping == MAP_FAILED) {
            const std::string reason = std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("cannot map RDB file '" + path + "': " + reason);
        }
        data_ = static_cast<const char*>(mapping);
#ifdef POSIX_MADV_SEQUENTIAL
        // 커널에 앞에서 뒤로 한 번 읽는 패턴임을 알려 캐시 동작을 돕는다.
        (void)::posix_madvise(const_cast<char*>(data_), size_, POSIX_MADV_SEQUENTIAL);
#endif
    }

    ~MappedFile() {
        if (data_ != 0) ::munmap(const_cast<char*>(data_), size_);
        if (fd_ >= 0) ::close(fd_);
    }

    const char* data() const { return data_ != 0 ? data_ : ""; }
    std::size_t size() const { return size_; }

private:
    MappedFile(const MappedFile&);
    MappedFile& operator=(const MappedFile&);

    int fd_;
    const char* data_;
    std::size_t size_;
};

class LineCursor {
public:
    explicit LineCursor(const MappedFile& file, CheckOffset start = 0)
        : data_(file.data()), end_(file.data() + file.size()), position_(file.data() + checked_start(file, start)) {}

    bool next(Line& line) {
        // memchr는 다음 줄바꿈을 빠르게 찾는다. getline처럼 줄 문자열을 할당하지 않는다.
        if (position_ == end_) return false;
        const char* const start = position_;
        const void* const newline = std::memchr(start, '\n', static_cast<std::size_t>(end_ - start));
        const char* raw_end = newline == 0 ? end_ : static_cast<const char*>(newline);
        position_ = newline == 0 ? end_ : raw_end + 1;
        const char* text_end = raw_end;
        if (text_end != start && *(text_end - 1) == '\r') --text_end;
        line.text = Span(start, text_end);
        line.offset = static_cast<CheckOffset>(start - data_);
        return true;
    }

    // 다음 줄 위치를 저장/복원한다. 다음 p/e 시그니처를 미리 읽은 뒤 되돌릴 때 쓴다.
    const char* mark() const { return position_; }
    void reset(const char* position) { position_ = position; }
    CheckOffset position() const { return static_cast<CheckOffset>(position_ - data_); }

private:
    static std::size_t checked_start(const MappedFile& file, CheckOffset start) {
        if (start > static_cast<CheckOffset>(file.size())) {
            throw ScanError(start, "check offset lies beyond end of file");
        }
        return static_cast<std::size_t>(start);
    }

    const char* data_;
    const char* end_;
    const char* position_;
};

inline bool next_nonblank(LineCursor& cursor, Line& line) {
    // 빈 줄은 RDB 의미에 영향을 주지 않으므로 호출자가 별도 처리하지 않게 한다.
    while (cursor.next(line)) {
        if (!trim(line.text).empty()) return true;
    }
    return false;
}

struct RuleHeader {
    // "현재 결과 수 원래 결과 수 check text 줄 수 시간" 헤더의 필요한 숫자만 보관한다.
    std::uint32_t current_result_count;
    std::uint32_t original_result_count;
    std::uint64_t check_text_line_count;
};

inline bool parse_rule_header(Span text, RuleHeader& result) {
    Span word;
    std::uint64_t current = 0;
    std::uint64_t original = 0;
    std::uint64_t text_lines = 0;
    if (!next_word(text, word) || !parse_unsigned(word, current) ||
        !next_word(text, word) || !parse_unsigned(word, original) ||
        !next_word(text, word) || !parse_unsigned(word, text_lines) || trim(text).empty() ||
        current > std::numeric_limits<std::uint32_t>::max() ||
        original > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    result.current_result_count = static_cast<std::uint32_t>(current);
    result.original_result_count = static_cast<std::uint32_t>(original);
    result.check_text_line_count = text_lines;
    return true;
}

struct ResultSignature {
    // p/e 한 줄에서 얻는 정보다. coordinate_count는 이어지는 좌표 행 개수다.
    ResultKind kind;
    std::uint32_t ordinal;
    std::uint64_t coordinate_count;
    Span suffix;
};

inline bool parse_result_signature(Span text, ResultSignature& result) {
    Span word;
    std::uint64_t ordinal = 0;
    std::uint64_t coordinate_count = 0;
    if (!next_word(text, word) || word.size() != 1U || (*word.begin != 'p' && *word.begin != 'e')) {
        return false;
    }
    const ResultKind kind = *word.begin == 'p' ? ResultKind::Polygon : ResultKind::EdgeCluster;
    if (
        !next_word(text, word) || !parse_unsigned(word, ordinal) ||
        !next_word(text, word) || !parse_unsigned(word, coordinate_count) ||
        ordinal > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    result.kind = kind;
    result.ordinal = static_cast<std::uint32_t>(ordinal);
    result.coordinate_count = coordinate_count;
    result.suffix = trim(text);
    return true;
}

inline bool starts_coordinate(Span text) {
    // 빠른 인덱서에서는 좌표 값을 저장하거나 완전히 변환하지 않는다.
    // 표준 RDB 좌표 행은 숫자 또는 +/-로 시작하고 태그 ID는 문자로 시작한다는 점을 이용한다.
    text = trim(text);
    if (text.empty()) return false;
    const char first = *text.begin;
    return (first >= '0' && first <= '9') || first == '+' || first == '-';
}

inline bool parse_point(Span text, Point& point) {
    // 상세/좌표 전용 파서는 실제 값이 필요하므로 두 정수만 허용한다.
    Span word;
    return next_word(text, word) && parse_signed(word, point.x) &&
           next_word(text, word) && parse_signed(word, point.y) && trim(text).empty();
}

inline bool parse_edge(Span text, Edge& edge) {
    // edge 행은 두 끝점(x1 y1 x2 y2)을 하나의 레코드로 저장한다.
    Span word;
    return next_word(text, word) && parse_signed(word, edge.first.x) &&
           next_word(text, word) && parse_signed(word, edge.first.y) &&
           next_word(text, word) && parse_signed(word, edge.second.x) &&
           next_word(text, word) && parse_signed(word, edge.second.y) && trim(text).empty();
}

inline void validate_database_header(const Line& line) {
    // 첫 줄의 마지막 단어가 양수 DBU인지 확인한다. Top cell 이름은 여기서 보관하지 않는다.
    const Span text = trim(line.text);
    Span cursor = text;
    Span word;
    Span last;
    while (next_word(cursor, word)) last = word;

    std::int64_t precision = 0;
    if (last.empty() || last.begin == text.begin || !parse_signed(last, precision) || precision <= 0) {
        throw ScanError(line.offset, "expected '<top cell name> <database precision>'");
    }
}

inline void skip_check_text(LineCursor& cursor, const RuleHeader& header) {
    // header에 명시된 줄 수만큼 그대로 건너뛴다. check text에는 임의의 문자가 올 수 있다.
    for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
        Line ignored;
        if (!cursor.next(ignored)) throw ScanError(cursor.position(), "truncated check text");
    }
}

inline bool next_result_signature(LineCursor& cursor, Line& signature_line, ResultSignature& signature) {
    // p/e 줄 앞에는 PP, CN 같은 태그가 있을 수 있으므로 시그니처를 만날 때까지 전진한다.
    while (next_nonblank(cursor, signature_line)) {
        if (parse_result_signature(trim(signature_line.text), signature)) return true;
    }
    return false;
}

inline void skip_fast_coordinates(LineCursor& cursor, const ResultSignature& signature) {
    // 1단계 인덱서는 좌표를 저장하지 않는다. 숫자로 시작하는 행만 개수에 반영한다.
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated result geometry");
        const Span text = trim(line.text);
        if (text.empty()) continue;
        if (starts_coordinate(text)) {
            ++seen;
            continue;
        }
        ResultSignature unexpected;
        if (parse_result_signature(text, unexpected)) {
            throw ScanError(line.offset, "result ended before its declared coordinate count");
        }
    }
}

// 본문을 저장하지 않고 "규칙 이름 줄 + 규칙 헤더 줄" 조합을 찾는다.
// 후보 다음 줄이 헤더가 아니면 한 줄만 전진해 다시 검사한다.
inline bool next_rule(LineCursor& cursor, Line& name_line, RuleHeader& header) {
    Line candidate;
    while (next_nonblank(cursor, candidate)) {
        const char* const after_candidate = cursor.mark();
        Line possible_header;
        if (!next_nonblank(cursor, possible_header)) return false;
        if (parse_rule_header(trim(possible_header.text), header)) {
            name_line = candidate;
            return true;
        }
        cursor.reset(after_candidate);
    }
    return false;
}

} // namespace detail

/*
 * 1단계용 빠른 스캐너다.
 *
 * 파일을 mmap으로 연결하고 규칙 이름, 파일 offset, 현재 p/e 결과 수만 결과 벡터에 넣는다.
 * 좌표와 태그는 저장하지 않고 건너뛰므로 TreeView를 먼저 빨리 만들 때 사용한다.
 */
class FastCheckIndexParser {
public:
    std::vector<CheckIndexEntry> parse_file(const std::string& path) const {
        detail::MappedFile file(path);
        detail::LineCursor cursor(file);
        detail::Line top_header;
        if (!detail::next_nonblank(cursor, top_header)) throw ScanError(0, "empty RDB file");
        detail::validate_database_header(top_header);

        std::vector<CheckIndexEntry> checks;
        detail::Line name_line;
        detail::RuleHeader header;
        while (detail::next_rule(cursor, name_line, header)) {
            // 이 세 필드가 1단계 파서의 최종 산출물이다.
            CheckIndexEntry entry;
            const detail::Span name = detail::trim(name_line.text);
            entry.name.assign(name.begin, name.size());
            entry.offset = name_line.offset;
            entry.geometry_count = header.current_result_count;
            checks.push_back(entry);

            detail::skip_check_text(cursor, header);
            for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
                // p/e 선언과 선언된 수만큼의 좌표 행만 지나간다.
                detail::Line signature_line;
                detail::ResultSignature signature;
                if (!detail::next_result_signature(cursor, signature_line, signature)) {
                    throw ScanError(cursor.position(), "truncated result list");
                }
                detail::skip_fast_coordinates(cursor, signature);
            }
        }
        return checks;
    }
};

} // namespace rdb

#endif // RDB_CHECK_INDEX_HPP
