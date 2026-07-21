#include "calibre_ascii_rdb_parser.hpp"

#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

namespace calibre {
namespace rdb {
namespace {

struct LineView {
    const char* data;
    std::size_t size;
    FileOffset offset;
    FileOffset next_offset;
    std::uint64_t number;

    LineView()
        : data(0), size(0), offset(0), next_offset(0), number(0) {}
};

struct StoredLine {
    std::string text;
    FileOffset offset;
    FileOffset next_offset;
    std::uint64_t number;

    StoredLine() : offset(0), next_offset(0), number(0) {}
};

struct Span {
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
    while (value.begin != value.end && is_space(*value.begin)) ++value.begin;
    while (value.begin != value.end && is_space(*(value.end - 1))) --value.end;
    return value;
}

Span content(const LineView& line) {
    const char* end = line.data + line.size;
    if (end != line.data && *(end - 1) == '\r') --end;
    return Span(line.data, end);
}

bool next_word(Span& input, Span& word) {
    input = trim(input);
    if (input.empty()) return false;
    const char* end = input.begin;
    while (end != input.end && !is_space(*end)) ++end;
    word = Span(input.begin, end);
    input.begin = end;
    return true;
}

bool parse_unsigned(Span value, std::uint64_t& result) {
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
    }

    ~BufferedLineReader() {
        if (fd_ >= 0) ::close(fd_);
    }

    FileOffset position() const {
        if (position_ < available_) return buffer_offset_ + static_cast<FileOffset>(position_);
        return next_read_offset_;
    }

    bool next(LineView& line) {
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

            append_overflow(begin, available_ - position_);
            has_overflow = true;
            position_ = available_;
        }
    }

private:
    bool refill() {
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
    ResultKind kind;
    std::uint64_t ordinal;
    std::uint64_t geometry_count;
    Span suffix;
};

struct RuleHeader {
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
    Span word;
    if (!next_word(text, word) || !parse_signed(word, result.x)) return false;
    if (!next_word(text, word) || !parse_signed(word, result.y)) return false;
    return trim(text).empty();
}

bool parse_edge(Span text, Edge& result) {
    Span word;
    if (!next_word(text, word) || !parse_signed(word, result.first.x)) return false;
    if (!next_word(text, word) || !parse_signed(word, result.first.y)) return false;
    if (!next_word(text, word) || !parse_signed(word, result.second.x)) return false;
    if (!next_word(text, word) || !parse_signed(word, result.second.y)) return false;
    return trim(text).empty();
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

class PropertyIdInterner {
public:
    StringId intern(StringTable& strings, Span value) {
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

    std::unordered_multimap<std::uint64_t, StringId> ids_;
};

class Parser {
public:
    Parser(const std::string& path, const ParseOptions& options)
        : reader_(path, options), options_(options) {}

    Database run() {
        Database database;
        database.rule_checks.reserve(1024);
        database.strings.reserve(2048, 128U * 1024U);

        LineView top_cell_header;
        if (!next_nonblank(top_cell_header)) fail_at(0, 1, "empty RDB file");
        parse_database_header(top_cell_header, database);

        LineView rule_name;
        while (next_nonblank(rule_name)) {
            parse_rule(rule_name, database);
        }
        return database;
    }

private:
    bool next_line(LineView& line) {
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
        pending_.push_front(rule_header);
        pending_.push_front(rule_name);
    }

    StringId add_text(Database& database, Span text) {
        return database.strings.add(text.begin, text.size());
    }

    void parse_database_header(const LineView& line, Database& database) {
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

        const Index text_begin = checked_index(database.check_text_lines.size(), "check-text begin");
        for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
            LineView text_line;
            if (!next_line(text_line)) fail_at(reader_.position(), header_line.number + i + 1U, "truncated check text");
            database.check_text_lines.push_back(add_text(database, content(text_line)));
        }
        rule.check_text = Range(text_begin,
                                checked_index(database.check_text_lines.size() - text_begin, "check-text count"));

        const Index result_begin = checked_index(database.results.size(), "result begin");
        for (std::uint32_t i = 0; i < rule.current_result_count; ++i) {
            Result result = parse_result(database);
            if (i + 1U == rule.current_result_count) {
                consume_final_result_tail(database, result);
            } else {
                consume_nonfinal_result_tail(database, result);
            }
            database.results.push_back(result);
        }
        rule.results = Range(result_begin,
                             checked_index(database.results.size() - result_begin, "result count"));

        if (rule.current_result_count == 0) {
            consume_empty_rule_boundary();
        }
        database.rule_checks.push_back(rule);
    }

    Result parse_result(Database& database) {
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

        std::uint64_t coordinates_seen = 0;
        while (coordinates_seen < signature.geometry_count) {
            LineView line;
            if (!next_line(line)) fail_at(reader_.position(), signature_line.number, "truncated result geometry");
            const Span text = trim(content(line));
            if (text.empty()) continue;

            if (signature.kind == ResultKind::Polygon) {
                Point point;
                if (parse_point(text, point)) {
                    database.vertices.push_back(point);
                    ++coordinates_seen;
                    continue;
                }
            } else {
                Edge edge;
                if (parse_edge(text, edge)) {
                    database.edges.push_back(edge);
                    ++coordinates_seen;
                    continue;
                }
            }

            ResultSignature unexpected;
            if (parse_result_signature(text, unexpected)) {
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
        const Index after_begin = checked_index(database.tagged_values.size(), "post-property begin");
        for (;;) {
            LineView line;
            if (!next_nonblank(line)) fail_at(reader_.position(), 0, "result count exceeds physical result list");
            ResultSignature next_result;
            if (parse_result_signature(trim(content(line)), next_result)) {
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
} // namespace calibre
