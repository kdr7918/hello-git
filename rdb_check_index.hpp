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

typedef std::uint64_t CheckOffset;

/* Thrown by the lightweight scanners.  The offset is relative to file start. */
class ScanError : public std::runtime_error {
public:
    ScanError(CheckOffset offset, const std::string& message)
        : std::runtime_error("RDB scan error at byte " + std::to_string(offset) + ": " + message),
          offset_(offset) {}

    CheckOffset offset() const { return offset_; }

private:
    CheckOffset offset_;
};

/* One TreeView-ready rule-check record.  geometry_count is the p/e result count. */
struct CheckIndexEntry {
    std::string name;
    CheckOffset offset;
    std::uint32_t geometry_count;

    CheckIndexEntry() : offset(0), geometry_count(0) {}
};

namespace detail {

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
    while (value.begin != value.end && space(*value.begin)) ++value.begin;
    while (value.begin != value.end && space(*(value.end - 1))) --value.end;
    return value;
}

inline bool next_word(Span& input, Span& word) {
    input = trim(input);
    if (input.empty()) return false;
    const char* end = input.begin;
    while (end != input.end && !space(*end)) ++end;
    word = Span(input.begin, end);
    input.begin = end;
    return true;
}

inline bool parse_unsigned(Span value, std::uint64_t& result) {
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
    while (cursor.next(line)) {
        if (!trim(line.text).empty()) return true;
    }
    return false;
}

struct RuleHeader {
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
    text = trim(text);
    if (text.empty()) return false;
    const char first = *text.begin;
    return (first >= '0' && first <= '9') || first == '+' || first == '-';
}

inline bool parse_point(Span text, Point& point) {
    Span word;
    return next_word(text, word) && parse_signed(word, point.x) &&
           next_word(text, word) && parse_signed(word, point.y) && trim(text).empty();
}

inline bool parse_edge(Span text, Edge& edge) {
    Span word;
    return next_word(text, word) && parse_signed(word, edge.first.x) &&
           next_word(text, word) && parse_signed(word, edge.first.y) &&
           next_word(text, word) && parse_signed(word, edge.second.x) &&
           next_word(text, word) && parse_signed(word, edge.second.y) && trim(text).empty();
}

inline void validate_database_header(const Line& line) {
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
    for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
        Line ignored;
        if (!cursor.next(ignored)) throw ScanError(cursor.position(), "truncated check text");
    }
}

inline bool next_result_signature(LineCursor& cursor, Line& signature_line, ResultSignature& signature) {
    while (next_nonblank(cursor, signature_line)) {
        if (parse_result_signature(trim(signature_line.text), signature)) return true;
    }
    return false;
}

inline void skip_fast_coordinates(LineCursor& cursor, const ResultSignature& signature) {
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

/* Finds a '<rule name>' + '<rule header>' pair without materializing body data. */
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
 * Fast stage-1 scanner.  It maps the input and only retains rule name, byte
 * offset, and the header's current p/e result count.  Coordinates and tags
 * are skipped without constructing strings or geometry records.
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
            CheckIndexEntry entry;
            const detail::Span name = detail::trim(name_line.text);
            entry.name.assign(name.begin, name.size());
            entry.offset = name_line.offset;
            entry.geometry_count = header.current_result_count;
            checks.push_back(entry);

            detail::skip_check_text(cursor, header);
            for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
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
