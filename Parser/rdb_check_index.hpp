#ifndef RDB_CHECK_INDEX_HPP
#define RDB_CHECK_INDEX_HPP

#include "ascii_rdb.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rdb {

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

class ScanCancelled : public std::runtime_error {
public:
    ScanCancelled() : std::runtime_error("RDB scan cancelled") {}
};

// TreeView의 한 행에 바로 사용할 수 있는 정보와 header 뒤 comment를 보관한다.
// geometry_count는 좌표 점 개수가 아니라 p/e 결과(결함 도형) 레코드 개수다.
struct CheckIndexEntry {
    std::string name;
    CheckOffset offset;
    std::uint32_t geometry_count;
    std::uint32_t original_result_count;
    std::uint32_t check_text_line_count;
    std::string comment; // 여러 comment 줄을 '\n'으로 연결하며 마지막 '\n'은 붙이지 않는다.

    CheckIndexEntry()
        : offset(0), geometry_count(0), original_result_count(0), check_text_line_count(0) {}
};

// 1단계 인덱싱 결과다. 파일 헤더와 TreeView용 Check 목록을 함께 반환한다.
struct CheckIndexDatabase {
    std::string top_cell_name;
    double database_precision;
    std::vector<CheckIndexEntry> checks;

    CheckIndexDatabase() : database_precision(0.0) {}
};

/*
 * 1단계 인덱서 전용 설정이다.
 * read_buffer_bytes는 시스템 호출 횟수와 캐시 사용량의 균형을 위한 값이다.
 * context_bytes는 버퍼 경계에서 바로 앞 Check Name 줄까지 다시 볼 수 있게 남겨 둔다.
 * progress_callback은 중복 없는 단조 증가 정수(0~100)를 전달하며, 100은 완료 후에만 전달한다.
 */
typedef std::function<void(int)> FastCheckIndexProgressCallback;
typedef std::function<bool()> FastCheckIndexCancellationCallback;

struct FastCheckIndexOptions {
    std::size_t read_buffer_bytes;
    std::size_t context_bytes;
    FastCheckIndexProgressCallback progress_callback;
    FastCheckIndexCancellationCallback is_cancelled;

    FastCheckIndexOptions()
        : read_buffer_bytes(16U * 1024U * 1024U),
          context_bytes(64U * 1024U),
          progress_callback(),
          is_cancelled() {}
};

namespace detail {

inline std::uint64_t standard_file_size(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open RDB file '" + path + "'");
    }
    const std::ifstream::pos_type end = input.tellg();
    if (end == std::ifstream::pos_type(-1)) {
        throw std::runtime_error("cannot determine RDB file size for '" + path + "'");
    }
    return static_cast<std::uint64_t>(
        static_cast<std::streamoff>(end));
}

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

inline bool parse_double(Span value, double& result) {
    // Header에서 한 번만 호출되는 경로다. classic locale로 소수점과 지수 표기를
    // 안정적으로 지원하고 NaN/Inf 및 뒤에 남는 문자는 거부한다.
    if (value.empty()) return false;
    const std::string token(value.begin, value.size());
    std::istringstream input(token);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> result;
    return input && input.peek() == std::char_traits<char>::eof() && std::isfinite(result);
}

struct Line {
    Span text;
    CheckOffset offset;
};

// 표준 C++ 스트림으로 파일을 읽어 Parser가 사용하는 연속 메모리를 제공한다.
class FileBuffer {
public:
    explicit FileBuffer(const std::string& path)
        : path_(path) {
        const std::uint64_t fileSize = standard_file_size(path);
        if (fileSize > static_cast<std::uint64_t>(bytes_.max_size())) {
            throw std::length_error("RDB file cannot fit in this process");
        }
        bytes_.resize(static_cast<std::size_t>(fileSize));
        if (bytes_.empty()) return;

        std::ifstream input(path.c_str(), std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open RDB file '" + path + "'");
        }
        std::size_t offset = 0U;
        const std::size_t maximumChunk = static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max());
        while (offset < bytes_.size()) {
            const std::size_t remaining = bytes_.size() - offset;
            const std::size_t chunk =
                remaining < maximumChunk ? remaining : maximumChunk;
            input.read(&bytes_[offset], static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                throw std::runtime_error(
                    "cannot read complete RDB file '" + path + "'");
            }
            offset += chunk;
        }
    }

    const char* data() const { return bytes_.empty() ? "" : &bytes_[0]; }
    std::size_t size() const { return bytes_.size(); }
    bool source_size_unchanged() const {
        try {
            return standard_file_size(path_) ==
                static_cast<std::uint64_t>(bytes_.size());
        } catch (...) {
            return false;
        }
    }

private:
    FileBuffer(const FileBuffer&);
    FileBuffer& operator=(const FileBuffer&);

    std::string path_;
    std::vector<char> bytes_;
};

class LineCursor {
public:
    explicit LineCursor(const FileBuffer& file, CheckOffset start = 0)
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
    static std::size_t checked_start(const FileBuffer& file, CheckOffset start) {
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
    // 상세 파서는 실제 값이 필요하므로 두 정수만 허용한다.
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

inline bool parse_database_header(Span value,
                                  std::string& top_cell_name,
                                  double& database_precision) {
    // 첫 nonblank 줄의 마지막 단어를 양수 DBU로, 앞부분을 Top cell 이름으로 해석한다.
    const Span text = trim(value);
    Span cursor = text;
    Span word;
    Span last;
    while (next_word(cursor, word)) last = word;

    double precision = 0.0;
    if (last.empty() || last.begin == text.begin || !parse_double(last, precision) || precision <= 0.0) {
        return false;
    }
    const Span top_cell = trim(Span(text.begin, last.begin));
    if (top_cell.empty()) return false;

    top_cell_name.assign(top_cell.begin, top_cell.size());
    database_precision = precision;
    return true;
}

inline void validate_database_header(const Line& line) {
    std::string top_cell_name;
    double database_precision = 0.0;
    if (!parse_database_header(line.text, top_cell_name, database_precision)) {
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

inline bool decimal(char value) {
    return value >= '0' && value <= '9';
}

inline bool parse_decimal_token(const char*& cursor, const char* end, std::uint64_t& value) {
    while (cursor != end && space(*cursor)) ++cursor;
    const char* const first = cursor;
    std::uint64_t parsed = 0;
    while (cursor != end && decimal(*cursor)) {
        const std::uint64_t digit = static_cast<std::uint64_t>(*cursor - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++cursor;
    }
    if (cursor == first) return false;
    value = parsed;
    return true;
}

inline bool timestamp_colon(const char* begin, const char* colon, const char* end) {
    // 첫 번째 ':'만 통과한다. 두 번째 ':'에서는 colon[-2]가 ':'이므로 자동으로 탈락한다.
    if (colon - begin < 2 || end - colon < 6) return false;
    if (colon - begin > 2 && !space(colon[-3])) return false;
    if (!decimal(colon[-2]) || !decimal(colon[-1]) ||
        !decimal(colon[1]) || !decimal(colon[2]) || colon[3] != ':' ||
        !decimal(colon[4]) || !decimal(colon[5])) {
        return false;
    }
    // 시간 뒤에 숫자/문자가 이어지면 HH:MM:SS의 일부가 아닐 가능성이 높다.
    if (end - colon > 6 && !space(colon[6]) && colon[6] != '\n') return false;

    const int hour = (colon[-2] - '0') * 10 + (colon[-1] - '0');
    const int minute = (colon[1] - '0') * 10 + (colon[2] - '0');
    const int second = (colon[4] - '0') * 10 + (colon[5] - '0');
    return hour < 24 && minute < 60 && second < 60;
}

struct CheckCommentRequest {
    std::size_t check_index;
    CheckOffset header_offset;
    std::uint64_t comment_count;

    CheckCommentRequest(std::size_t index,
                        CheckOffset offset,
                        std::uint64_t count)
        : check_index(index), header_offset(offset), comment_count(count) {}
};

inline bool make_check_index_entry(const char* buffer_begin,
                                   const char* buffer_end,
                                   const char* time_colon,
                                   CheckOffset buffer_offset,
                                   bool buffer_starts_at_line_boundary,
                                   CheckIndexEntry& entry,
                                   CheckOffset& header_offset,
                                   std::uint64_t& comment_count) {
    // 시간 줄의 시작을 찾는다. 이 줄은 "현재 수 원래 수 text 줄 수 날짜 시간" 헤더여야 한다.
    const char* header_begin = time_colon;
    while (header_begin != buffer_begin && header_begin[-1] != '\n') --header_begin;
    if (header_begin == buffer_begin) {
        if (buffer_offset != 0) {
            throw ScanError(buffer_offset, "fast index context does not contain the previous Check name line");
        }
        return false; // 파일 첫 줄에는 직전 Check Name이 없다.
    }

    // 헤더 바로 앞 줄이 Check Name이다. 버퍼 경계에서 잘린 이름은 오인식을 피하려고 건너뛴다.
    const char* name_end = header_begin - 1;
    if (name_end != buffer_begin && name_end[-1] == '\r') --name_end;
    const char* name_begin = name_end;
    while (name_begin != buffer_begin && name_begin[-1] != '\n') --name_begin;
    if (name_begin == buffer_begin && buffer_offset != 0 && !buffer_starts_at_line_boundary) {
        throw ScanError(buffer_offset, "fast index context starts inside a Check name line");
    }
    const Span name = trim(Span(name_begin, name_end));
    if (name.empty()) return false;

    // 시간 전까지의 처음 세 숫자만 읽는다. 이 검사가 ':'가 들어 있는 일반 comment를 걸러 낸다.
    const char* cursor = header_begin;
    std::uint64_t current = 0;
    std::uint64_t original = 0;
    std::uint64_t text_lines = 0;
    if (!parse_decimal_token(cursor, buffer_end, current) ||
        !parse_decimal_token(cursor, buffer_end, original) ||
        !parse_decimal_token(cursor, buffer_end, text_lines) ||
        cursor > time_colon) {
        return false;
    }
    const CheckOffset parsed_header_offset =
        buffer_offset + static_cast<CheckOffset>(header_begin - buffer_begin);
    if (current > std::numeric_limits<std::uint32_t>::max() ||
        original > std::numeric_limits<std::uint32_t>::max() ||
        text_lines > std::numeric_limits<std::uint32_t>::max()) {
        throw ScanError(parsed_header_offset, "rule-check count exceeds 32-bit capacity");
    }
    if (current > original) {
        throw ScanError(parsed_header_offset,
            "current result count exceeds original result count");
    }

    entry.name.assign(name.begin, name.size());
    entry.offset = buffer_offset + static_cast<CheckOffset>(name_begin - buffer_begin);
    entry.geometry_count = static_cast<std::uint32_t>(current);
    entry.original_result_count = static_cast<std::uint32_t>(original);
    entry.check_text_line_count = static_cast<std::uint32_t>(text_lines);
    header_offset = buffer_offset + static_cast<CheckOffset>(header_begin - buffer_begin);
    comment_count = text_lines;
    return true;
}

/*
 * 최속 1단계 경로다. 전체 문법을 따라가지 않고 시간의 첫 ':'만 검색한다.
 * read() 버퍼 끝에는 context_bytes를 남겨, 다음 버퍼에서 앞선 Check Name 줄을 찾을 수 있게 한다.
 */
class HeaderPatternIndexReader {
public:
    HeaderPatternIndexReader(const std::string& path, const FastCheckIndexOptions& options)
        : path_(path),
          stream_(path.c_str(), std::ios::binary),
          random_stream_(path.c_str(), std::ios::binary),
          initial_size_(0U),
          progress_callback_(options.progress_callback),
          cancellation_callback_(options.is_cancelled),
          last_progress_(-1),
          context_bytes_(options.context_bytes),
          buffer_offset_(0),
          read_offset_(0),
          buffer_starts_at_line_boundary_(true) {
        if (options.read_buffer_bytes == 0 || options.context_bytes < 64U) {
            throw std::invalid_argument("fast RDB index buffer/context size is too small");
        }
        if (options.read_buffer_bytes > std::numeric_limits<std::size_t>::max() - options.context_bytes) {
            throw std::length_error("fast RDB index buffer size overflows size_t");
        }
        buffer_.resize(options.read_buffer_bytes + options.context_bytes);
        if (!stream_ || !random_stream_) {
            throw std::runtime_error("cannot open RDB file '" + path + "'");
        }
        initial_size_ = standard_file_size(path);
    }

    CheckIndexDatabase run() {
        check_cancelled();
        report_progress(0);
        CheckIndexDatabase database;
        std::vector<CheckCommentRequest> comment_requests;
        CheckOffset bytes_scanned = 0;
        bool database_header_parsed = false;
        std::size_t carried = 0;
        std::size_t scan_begin = 0;
        const std::size_t lookahead_bytes = 7U; // HH:MM:SS 뒤 문자까지 확인할 수 있는 최소 여유

        for (;;) {
            check_cancelled();
            const std::streamsize received =
                read_block(&buffer_[carried], buffer_.size() - carried);
            const bool eof = received == 0;
            const std::size_t total = carried + (received > 0 ? static_cast<std::size_t>(received) : 0U);
            if (received > 0) {
                bytes_scanned += static_cast<CheckOffset>(received);
                report_scan_progress(bytes_scanned);
            }

            if (!database_header_parsed) {
                database_header_parsed = try_parse_database_header(total, eof, database);
            }

            // ':' 뒤에 HH:MM:SS가 모두 들어 있는 후보만 처리한다.
            // 버퍼 끝 7바이트는 다음 read 후 검사한다.
            const std::size_t scan_end = eof ? total :
                (total > lookahead_bytes ? total - lookahead_bytes : 0U);
            if (scan_begin < scan_end) {
                scan_headers(
                    total, scan_begin, scan_end, database.checks, comment_requests);
            }

            if (eof) break;

            // 아직 검사하지 않은 tail이 속한 줄(보통 헤더)과 그 직전 Check Name 줄은
            // context_bytes보다 길더라도 통째로 남긴다. 임의 바이트 경계에서 잘라 유효한
            // Check를 누락시키지 않으며, 매우 긴 두 줄은 버퍼를 기하급수적으로 확장한다.
            std::size_t current_line_start = scan_end;
            while (current_line_start != 0 && buffer_[current_line_start - 1U] != '\n') {
                --current_line_start;
            }
            std::size_t required_start = current_line_start;
            if (required_start != 0) {
                --required_start; // 현재 줄 앞의 '\n'
                while (required_start != 0 && buffer_[required_start - 1U] != '\n') {
                    --required_start;
                }
            }
            const std::size_t context_start = total > context_bytes_ ? total - context_bytes_ : 0U;
            const std::size_t consumed = context_start < required_start ? context_start : required_start;
            carried = total - consumed;
            const bool next_buffer_starts_at_line_boundary =
                consumed == 0 || buffer_[consumed - 1U] == '\n';
            if (consumed == 0 && carried == buffer_.size()) {
                if (buffer_.size() > buffer_.max_size() / 2U) {
                    throw std::length_error("fast RDB index context exceeds vector capacity");
                }
                buffer_.resize(buffer_.size() * 2U);
            } else if (carried != 0 && consumed != 0) {
                std::memmove(&buffer_[0], &buffer_[consumed], carried);
            }
            buffer_offset_ += static_cast<CheckOffset>(consumed);
            buffer_starts_at_line_boundary_ = next_buffer_starts_at_line_boundary;
            scan_begin = carried > lookahead_bytes ? carried - lookahead_bytes : 0U;
        }
        verify_file_unchanged();
        report_progress(90);
        populate_comments(database, comment_requests);
        verify_file_unchanged();
        check_cancelled();
        report_progress(100);
        return database;
    }

private:
    void check_cancelled() const {
        if (cancellation_callback_ && cancellation_callback_()) throw ScanCancelled();
    }

    void report_progress(int value) const {
        if (!progress_callback_) return;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        if (value <= last_progress_) return;
        last_progress_ = value;
        progress_callback_(value);
    }

    void report_scan_progress(CheckOffset bytes_scanned) const {
        if (!progress_callback_ || initial_size_ == 0U) return;
        const CheckOffset file_size = static_cast<CheckOffset>(initial_size_);
        if (bytes_scanned >= file_size) {
            report_progress(90);
            return;
        }
        const long double ratio = static_cast<long double>(bytes_scanned) /
            static_cast<long double>(file_size);
        report_progress(static_cast<int>(ratio * 90.0L));
    }

    void report_comment_progress(std::size_t completed, std::size_t total) const {
        if (!progress_callback_ || total == 0) return;
        if (completed >= total) {
            report_progress(99);
            return;
        }
        const long double ratio = static_cast<long double>(completed) /
            static_cast<long double>(total);
        report_progress(90 + static_cast<int>(ratio * 9.0L));
    }

    void verify_file_unchanged() const {
        if (standard_file_size(path_) != initial_size_) {
            throw ScanError(0, "RDB file changed while fast indexing");
        }
    }

    bool try_parse_database_header(std::size_t total_size,
                                   bool eof,
                                   CheckIndexDatabase& database) const {
        const char* const begin = &buffer_[0];
        const char* const end = begin + total_size;
        const char* cursor = begin;

        while (cursor != end) {
            const void* const found =
                std::memchr(cursor, '\n', static_cast<std::size_t>(end - cursor));
            if (found == 0 && !eof) return false;

            const char* const raw_end =
                found == 0 ? end : static_cast<const char*>(found);
            const Span line = trim(Span(cursor, raw_end));
            if (!line.empty()) {
                if (!parse_database_header(
                        line, database.top_cell_name, database.database_precision)) {
                    const CheckOffset offset = buffer_offset_ +
                        static_cast<CheckOffset>(cursor - begin);
                    throw ScanError(
                        offset, "expected '<top cell name> <database precision>'");
                }
                return true;
            }
            if (found == 0) break;
            cursor = raw_end + 1;
        }

        if (eof) throw ScanError(0, "empty RDB file");
        return false;
    }

    bool finish_comment_line(CheckIndexEntry& entry,
                             std::size_t expected_comments,
                             std::size_t& lines_seen,
                             std::string& line) const {
        if (!line.empty() && line[line.size() - 1U] == '\r') line.resize(line.size() - 1U);
        if (lines_seen != 0) {
            if (lines_seen > 1U) entry.comment.push_back('\n');
            entry.comment.append(line);
        }
        line.clear();
        ++lines_seen;
        return lines_seen == expected_comments + 1U;
    }

    std::streamsize pread_block(char* destination,
                                std::size_t capacity,
                                CheckOffset offset) const {
        if (offset > static_cast<CheckOffset>(
                         std::numeric_limits<std::streamoff>::max())) {
            throw ScanError(offset, "comment offset exceeds stream range");
        }
        const std::size_t maximum = static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max());
        const std::size_t request =
            capacity < maximum ? capacity : maximum;
        random_stream_.clear();
        random_stream_.seekg(
            static_cast<std::streamoff>(offset), std::ios::beg);
        if (!random_stream_) {
            throw std::runtime_error("cannot seek RDB comments");
        }
        random_stream_.read(
            destination, static_cast<std::streamsize>(request));
        const std::streamsize received = random_stream_.gcount();
        if (random_stream_.bad()) {
            throw std::runtime_error("cannot read RDB comments");
        }
        return received;
    }

    CheckOffset populate_entry_comments(
        CheckIndexEntry& entry,
        const CheckCommentRequest& request,
        std::vector<char>& block) const {
        if (request.comment_count > entry.comment.max_size() ||
            request.comment_count == std::numeric_limits<std::size_t>::max()) {
            throw ScanError(request.header_offset, "comment count exceeds platform capacity");
        }

        const std::size_t expected_comments =
            static_cast<std::size_t>(request.comment_count);
        std::size_t lines_seen = 0;
        CheckOffset offset = request.header_offset;
        std::string line;

        for (;;) {
            check_cancelled();
            const std::streamsize received =
                pread_block(&block[0], block.size(), offset);
            if (received == 0) {
                if (!line.empty() &&
                    finish_comment_line(entry, expected_comments, lines_seen, line)) {
                    return offset;
                }
                throw ScanError(offset, "truncated check comments");
            }

            const std::size_t size = static_cast<std::size_t>(received);
            for (std::size_t i = 0; i < size; ++i) {
                const char value = block[i];
                if (value == '\n') {
                    if (finish_comment_line(
                            entry, expected_comments, lines_seen, line)) {
                        return offset + static_cast<CheckOffset>(i + 1U);
                    }
                } else {
                    line.push_back(value);
                }
            }
            offset += static_cast<CheckOffset>(size);
        }
    }

    void populate_comments(CheckIndexDatabase& database,
                           const std::vector<CheckCommentRequest>& requests) const {
        std::vector<char> block(4U * 1024U);
        std::vector<CheckIndexEntry> filtered_checks;
        filtered_checks.reserve(database.checks.size());
        CheckOffset comments_end = 0;

        for (std::size_t request_index = 0; request_index < requests.size(); ++request_index) {
            check_cancelled();
            const CheckCommentRequest& request = requests[request_index];
            CheckIndexEntry& raw_entry = database.checks[request.check_index];
            if (raw_entry.offset < comments_end) {
                // Comment 안에 우연히 나온 "이름 + 숫자 3개 + HH:MM:SS" 패턴이다.
                report_comment_progress(request_index + 1U, requests.size());
                continue;
            }

            CheckIndexEntry entry = std::move(raw_entry);
            comments_end = populate_entry_comments(entry, request, block);
            filtered_checks.push_back(std::move(entry));
            report_comment_progress(request_index + 1U, requests.size());
        }
        database.checks.swap(filtered_checks);
    }

    std::streamsize read_block(char* destination, std::size_t capacity) {
        const std::size_t maximum = static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max());
        const std::size_t request =
            capacity < maximum ? capacity : maximum;
        stream_.read(destination, static_cast<std::streamsize>(request));
        const std::streamsize received = stream_.gcount();
        if (stream_.bad()) {
            throw std::runtime_error("cannot read RDB file");
        }
        read_offset_ += static_cast<CheckOffset>(received);
        return received;
    }

    void scan_headers(std::size_t total_size,
                      std::size_t scan_begin,
                      std::size_t scan_end_size,
                      std::vector<CheckIndexEntry>& checks,
                      std::vector<CheckCommentRequest>& comment_requests) const {
        const char* const begin = &buffer_[0];
        const char* const end = begin + total_size;
        const char* cursor = begin + scan_begin;
        const char* const scan_end = begin + scan_end_size;
        while (cursor != scan_end) {
            const void* const found = std::memchr(cursor, ':', static_cast<std::size_t>(scan_end - cursor));
            if (found == 0) return;
            const char* const colon = static_cast<const char*>(found);
            if (timestamp_colon(begin, colon, end)) {
                CheckIndexEntry entry;
                CheckOffset header_offset = 0;
                std::uint64_t comment_count = 0;
                if (make_check_index_entry(
                        begin, end, colon, buffer_offset_, buffer_starts_at_line_boundary_,
                        entry, header_offset, comment_count)) {
                    checks.push_back(entry);
                    comment_requests.push_back(CheckCommentRequest(
                        checks.size() - 1U, header_offset, comment_count));
                }
            }
            cursor = colon + 1;
        }
    }

    HeaderPatternIndexReader(const HeaderPatternIndexReader&);
    HeaderPatternIndexReader& operator=(const HeaderPatternIndexReader&);

    std::string path_;
    std::ifstream stream_;
    mutable std::ifstream random_stream_;
    std::uint64_t initial_size_;
    FastCheckIndexProgressCallback progress_callback_;
    FastCheckIndexCancellationCallback cancellation_callback_;
    mutable int last_progress_;
    std::vector<char> buffer_;
    std::size_t context_bytes_;
    CheckOffset buffer_offset_;
    CheckOffset read_offset_;
    bool buffer_starts_at_line_boundary_;
};

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
 * 1단계용 최고속 스캐너다.
 *
 * read() 버퍼에서 HH:MM:SS의 첫 ':'만 찾고, 그 줄의 처음 세 숫자를 검증한다.
 * 좌표, 태그, p/e 선언은 해석하지 않지만 header의 text-line count만큼
 * comment를 원문 문자열로 수집한다.
 * 이 경로는 표준적인 "숫자 3개 + 날짜 + HH:MM:SS" RuleCheck 헤더를 전제한다.
 */
class FastCheckIndexParser {
public:
    CheckIndexDatabase parse_database(
        const std::string& path,
        const FastCheckIndexOptions& options = FastCheckIndexOptions()) const {
        return detail::HeaderPatternIndexReader(path, options).run();
    }

    // 기존 Check 목록 전용 API를 유지한다.
    std::vector<CheckIndexEntry> parse_file(
        const std::string& path,
        const FastCheckIndexOptions& options = FastCheckIndexOptions()) const {
        return parse_database(path, options).checks;
    }

};

} // namespace rdb

#endif // RDB_CHECK_INDEX_HPP
