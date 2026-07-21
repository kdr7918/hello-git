#ifndef FAST_TEXT_PARSER_HPP
#define FAST_TEXT_PARSER_HPP

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <locale.h>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define FASTTEXT_HAS_MMAP 1
#else
#define FASTTEXT_HAS_MMAP 0
#endif

#ifndef FASTTEXT_STRTOD_L_BACKEND
#if defined(_WIN32)
#define FASTTEXT_STRTOD_L_BACKEND 2
#elif defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || \
      defined(__NetBSD__) || defined(__OpenBSD__)
#define FASTTEXT_STRTOD_L_BACKEND 1
#else
#define FASTTEXT_STRTOD_L_BACKEND 0
#endif
#endif

namespace fasttext {

static const std::size_t npos = static_cast<std::size_t>(-1);

class StringView {
public:
    const char* data;
    std::size_t size;
    std::size_t offset;

    StringView() : data(NULL), size(0), offset(0) {}
    explicit StringView(const char* text)
        : data(text), size(text == NULL ? 0 : std::strlen(text)), offset(0) {}
    explicit StringView(const std::string& text)
        : data(text.data()), size(text.size()), offset(0) {}
    StringView(const char* text, std::size_t length, std::size_t absolute_offset = 0)
        : data(text), size(text == NULL ? 0 : length), offset(absolute_offset) {}

    bool empty() const { return size == 0; }
    const char* begin() const { return data; }
    const char* end() const { return data == NULL ? NULL : data + size; }
    char operator[](std::size_t index) const { return data[index]; }

    StringView substr(std::size_t position, std::size_t count = npos) const {
        if (position > size) position = size;
        const std::size_t available = size - position;
        if (count > available) count = available;
        return StringView(data == NULL ? NULL : data + position, count, offset + position);
    }

    std::string to_string() const {
        return data == NULL ? std::string() : std::string(data, size);
    }

    bool is_alpha() const;
    bool is_integer() const;
    bool is_number() const;
    bool to_int64(std::int64_t* output) const;
    bool to_uint64(std::uint64_t* output) const;
    bool to_double(double* output) const;
    std::int64_t int64_value(std::int64_t fallback = 0) const;
    std::uint64_t uint64_value(std::uint64_t fallback = 0) const;
    double number_value(double fallback = 0.0) const;
};

inline bool operator==(const StringView& left, const StringView& right) {
    return left.size == right.size &&
           (left.size == 0 || std::memcmp(left.data, right.data, left.size) == 0);
}

inline bool operator!=(const StringView& left, const StringView& right) {
    return !(left == right);
}

inline bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' ||
           value == '\n' || value == '\f' || value == '\v';
}

inline bool is_digit(char value) { return value >= '0' && value <= '9'; }
inline bool is_alpha(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}
inline bool is_word_char(char value) {
    return is_alpha(value) || is_digit(value) || value == '_';
}

inline StringView trim_left(StringView value) {
    std::size_t index = 0;
    while (index < value.size && is_space(value[index])) ++index;
    return value.substr(index);
}

inline StringView trim_right(StringView value) {
    std::size_t length = value.size;
    while (length != 0 && is_space(value[length - 1])) --length;
    return StringView(value.data, length, value.offset);
}

inline StringView trim(StringView value) { return trim_right(trim_left(value)); }

inline std::size_t find_char(StringView haystack, char needle, std::size_t from = 0) {
    if (from >= haystack.size || haystack.data == NULL) return npos;
    const void* found = std::memchr(haystack.data + from, static_cast<unsigned char>(needle),
                                    haystack.size - from);
    return found == NULL ? npos : static_cast<const char*>(found) - haystack.data;
}

inline std::size_t find(StringView haystack, StringView needle, std::size_t from = 0) {
    if (from > haystack.size) return npos;
    if (needle.size == 0) return from;
    if (needle.size > haystack.size - from || haystack.data == NULL || needle.data == NULL)
        return npos;
    const char* cursor = haystack.data + from;
    const char* const last = haystack.data + haystack.size - needle.size + 1;
    while (cursor < last) {
        const void* candidate = std::memchr(cursor, static_cast<unsigned char>(needle[0]),
                                            static_cast<std::size_t>(last - cursor));
        if (candidate == NULL) return npos;
        cursor = static_cast<const char*>(candidate);
        if (std::memcmp(cursor, needle.data, needle.size) == 0)
            return static_cast<std::size_t>(cursor - haystack.data);
        ++cursor;
    }
    return npos;
}

inline bool contains_word(StringView haystack, StringView word) {
    std::size_t position = 0;
    while ((position = find(haystack, word, position)) != npos) {
        const bool left_ok = position == 0 || !is_word_char(haystack[position - 1]);
        const std::size_t end = position + word.size;
        const bool right_ok = end == haystack.size || !is_word_char(haystack[end]);
        if (left_ok && right_ok) return true;
        ++position;
    }
    return false;
}

template <typename Callback>
inline void split_each(StringView value, char delimiter, Callback callback) {
    std::size_t start = 0;
    while (start <= value.size) {
        const std::size_t delimiter_position = find_char(value, delimiter, start);
        if (delimiter_position == npos) {
            callback(value.substr(start));
            return;
        }
        callback(value.substr(start, delimiter_position - start));
        start = delimiter_position + 1;
    }
}

inline std::vector<StringView> split(StringView value, char delimiter) {
    std::vector<StringView> result;
    result.reserve(8);
    split_each(value, delimiter, [&](StringView part) { result.push_back(part); });
    return result;
}

struct Match {
    std::size_t offset;
    std::size_t length;
    Match() : offset(npos), length(0) {}
};

inline bool regex_find(StringView value, const std::regex& expression, Match* output) {
    if (value.data == NULL) return false;
    std::match_results<const char*> result;
    if (!std::regex_search(value.data, value.data + value.size, result, expression)) return false;
    if (output != NULL) {
        output->offset = value.offset + static_cast<std::size_t>(result[0].first - value.data);
        output->length = static_cast<std::size_t>(result[0].second - result[0].first);
    }
    return true;
}

#if FASTTEXT_STRTOD_L_BACKEND == 1
inline locale_t numeric_c_locale() {
    // Intentionally process-lifetime: avoids static-destruction-order use-after-free.
    static locale_t* locale = new locale_t(
        ::newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0)));
    return *locale;
}
#elif FASTTEXT_STRTOD_L_BACKEND == 2
inline _locale_t numeric_c_locale() {
    // Intentionally process-lifetime: avoids static-destruction-order use-after-free.
    static _locale_t* locale = new _locale_t(::_create_locale(LC_NUMERIC, "C"));
    return *locale;
}
#endif

class Cursor {
public:
    explicit Cursor(StringView input) : input_(input), position_(0) {}

    std::size_t position() const { return position_; }
    std::size_t absolute_offset() const { return input_.offset + position_; }
    bool empty() const { return position_ >= input_.size; }
    StringView remaining() const { return input_.substr(position_); }

    void skip_spaces() {
        while (position_ < input_.size && is_space(input_[position_])) ++position_;
    }

    bool read_char(char expected) {
        if (position_ >= input_.size || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    StringView read_alpha() {
        const std::size_t start = position_;
        while (position_ < input_.size && is_alpha(input_[position_])) ++position_;
        return input_.substr(start, position_ - start);
    }

    StringView read_while_digits() {
        const std::size_t start = position_;
        while (position_ < input_.size && is_digit(input_[position_])) ++position_;
        return input_.substr(start, position_ - start);
    }

    StringView read_word() {
        const std::size_t start = position_;
        while (position_ < input_.size && !is_space(input_[position_])) ++position_;
        return input_.substr(start, position_ - start);
    }

    StringView read_identifier() {
        const std::size_t start = position_;
        if (position_ < input_.size && (is_alpha(input_[position_]) || input_[position_] == '_')) {
            ++position_;
            while (position_ < input_.size && is_word_char(input_[position_])) ++position_;
        }
        return input_.substr(start, position_ - start);
    }

    bool read_uint64(std::uint64_t* output) {
        const std::size_t original = position_;
        std::size_t cursor = position_;
        if (cursor < input_.size && input_[cursor] == '+') ++cursor;
        if (cursor >= input_.size || !is_digit(input_[cursor])) return false;
        std::uint64_t value = 0;
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        while (cursor < input_.size && is_digit(input_[cursor])) {
            const unsigned digit = static_cast<unsigned>(input_[cursor] - '0');
            if (value > (maximum - digit) / 10u) {
                position_ = original;
                return false;
            }
            value = value * 10u + digit;
            ++cursor;
        }
        position_ = cursor;
        if (output != NULL) *output = value;
        return true;
    }

    bool read_int64(std::int64_t* output) {
        const std::size_t original = position_;
        std::size_t cursor = position_;
        bool negative = false;
        if (cursor < input_.size && (input_[cursor] == '-' || input_[cursor] == '+')) {
            negative = input_[cursor] == '-';
            ++cursor;
        }
        if (cursor >= input_.size || !is_digit(input_[cursor])) return false;
        const std::uint64_t positive_limit =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        const std::uint64_t limit = negative ? positive_limit + 1u : positive_limit;
        std::uint64_t magnitude = 0;
        while (cursor < input_.size && is_digit(input_[cursor])) {
            const unsigned digit = static_cast<unsigned>(input_[cursor] - '0');
            if (magnitude > (limit - digit) / 10u) {
                position_ = original;
                return false;
            }
            magnitude = magnitude * 10u + digit;
            ++cursor;
        }
        position_ = cursor;
        if (output != NULL) {
            if (negative && magnitude == positive_limit + 1u)
                *output = std::numeric_limits<std::int64_t>::min();
            else if (negative)
                *output = -static_cast<std::int64_t>(magnitude);
            else
                *output = static_cast<std::int64_t>(magnitude);
        }
        return true;
    }

    bool read_double(double* output) {
        const std::size_t original = position_;
        std::size_t cursor = position_;
        if (cursor < input_.size && (input_[cursor] == '-' || input_[cursor] == '+')) ++cursor;
        bool has_digit = false;
        bool has_nonzero_digit = false;
        while (cursor < input_.size && is_digit(input_[cursor])) {
            has_digit = true;
            has_nonzero_digit = has_nonzero_digit || input_[cursor] != '0';
            ++cursor;
        }
        if (cursor < input_.size && input_[cursor] == '.') {
            ++cursor;
            while (cursor < input_.size && is_digit(input_[cursor])) {
                has_digit = true;
                has_nonzero_digit = has_nonzero_digit || input_[cursor] != '0';
                ++cursor;
            }
        }
        if (!has_digit) return false;
        if (cursor < input_.size && (input_[cursor] == 'e' || input_[cursor] == 'E')) {
            const std::size_t exponent = cursor++;
            if (cursor < input_.size && (input_[cursor] == '-' || input_[cursor] == '+')) ++cursor;
            const std::size_t exponent_digits = cursor;
            while (cursor < input_.size && is_digit(input_[cursor])) ++cursor;
            if (cursor == exponent_digits) cursor = exponent;
        }
        const std::size_t length = cursor - original;
        char local[128];
        std::string large;
        const char* token = NULL;
        if (length < sizeof(local)) {
            std::memcpy(local, input_.data + original, length);
            local[length] = '\0';
            token = local;
        } else {
            large.assign(input_.data + original, length);
            token = large.c_str();
        }
        errno = 0;
        double value = 0.0;
#if FASTTEXT_STRTOD_L_BACKEND == 1
        const locale_t locale = numeric_c_locale();
        if (locale == static_cast<locale_t>(0)) return false;
        char* end = NULL;
        value = ::strtod_l(token, &end, locale);
        if (end != token + length) return false;
#elif FASTTEXT_STRTOD_L_BACKEND == 2
        const _locale_t locale = numeric_c_locale();
        if (locale == NULL) return false;
        char* end = NULL;
        value = ::_strtod_l(token, &end, locale);
        if (end != token + length) return false;
#else
        std::istringstream stream(std::string(token, length));
        stream.imbue(std::locale::classic());
        stream >> value;
        if (!stream || stream.peek() != std::char_traits<char>::eof()) return false;
#endif
        if (!std::isfinite(value) || (value == 0.0 && has_nonzero_digit)) return false;
        position_ = cursor;
        if (output != NULL) *output = value;
        return true;
    }

private:
    StringView input_;
    std::size_t position_;
};

inline bool StringView::is_alpha() const {
    if (empty()) return false;
    for (std::size_t index = 0; index < size; ++index) {
        if (!fasttext::is_alpha(data[index])) return false;
    }
    return true;
}

inline bool StringView::is_integer() const {
    if (empty()) return false;
    std::size_t index = 0;
    if (data[index] == '+' || data[index] == '-') ++index;
    if (index == size) return false;
    for (; index < size; ++index) {
        if (!is_digit(data[index])) return false;
    }
    return true;
}

inline bool StringView::to_int64(std::int64_t* output) const {
    Cursor cursor(*this);
    std::int64_t value = 0;
    if (!cursor.read_int64(&value) || !cursor.empty()) return false;
    if (output != NULL) *output = value;
    return true;
}

inline bool StringView::to_uint64(std::uint64_t* output) const {
    Cursor cursor(*this);
    std::uint64_t value = 0;
    if (!cursor.read_uint64(&value) || !cursor.empty()) return false;
    if (output != NULL) *output = value;
    return true;
}

inline bool StringView::to_double(double* output) const {
    Cursor cursor(*this);
    double value = 0.0;
    if (!cursor.read_double(&value) || !cursor.empty()) return false;
    if (output != NULL) *output = value;
    return true;
}

inline bool StringView::is_number() const {
    return to_double(NULL);
}

inline std::int64_t StringView::int64_value(std::int64_t fallback) const {
    std::int64_t value = 0;
    return to_int64(&value) ? value : fallback;
}

inline std::uint64_t StringView::uint64_value(std::uint64_t fallback) const {
    std::uint64_t value = 0;
    return to_uint64(&value) ? value : fallback;
}

inline double StringView::number_value(double fallback) const {
    double value = 0.0;
    return to_double(&value) ? value : fallback;
}

class ReadBuffer {
public:
    ReadBuffer() {}

    void clear() { bytes_.clear(); }

    bool load_file(const std::string& path, std::string* error = NULL,
                   std::size_t block_size = 1024u * 1024u) {
        std::unique_ptr<FILE, FileCloser> file(std::fopen(path.c_str(), "rb"));
        if (!file) {
            set_error(error, "fopen", path);
            return false;
        }
        const bool ok = load(file.get(), error, block_size);
        FILE* const raw_file = file.release();
        if (std::fclose(raw_file) != 0 && ok) {
            set_error(error, "fclose", path);
            return false;
        }
        return ok;
    }

    bool load(FILE* file, std::string* error = NULL,
              std::size_t block_size = 1024u * 1024u) {
        bytes_.clear();
        if (file == NULL || block_size == 0) {
            if (error != NULL) *error = "invalid FILE* or block size";
            return false;
        }
        for (;;) {
            const std::size_t old_size = bytes_.size();
            if (block_size > bytes_.max_size() - old_size) {
                if (error != NULL) *error = "input exceeds ReadBuffer maximum size";
                bytes_.clear();
                return false;
            }
            bytes_.resize(old_size + block_size);
            const std::size_t count = std::fread(&bytes_[old_size], 1, block_size, file);
            bytes_.resize(old_size + count);
            if (count < block_size) {
                if (std::ferror(file)) {
                    if (error != NULL) *error = "fread failed";
                    bytes_.clear();
                    return false;
                }
                break;
            }
        }
        return true;
    }

    const char* data() const { return bytes_.empty() ? empty_data() : &bytes_[0]; }
    std::size_t size() const { return bytes_.size(); }
    StringView view() const { return StringView(data(), size(), 0); }

private:
    struct FileCloser {
        void operator()(FILE* file) const {
            if (file != NULL) std::fclose(file);
        }
    };

    static const char* empty_data() {
        static const char value = '\0';
        return &value;
    }

    static void set_error(std::string* error, const char* operation, const std::string& path) {
        if (error != NULL)
            *error = std::string(operation) + " failed for " + path + ": " + std::strerror(errno);
    }

    std::vector<char> bytes_;
};

class MappedFile {
public:
    MappedFile()
        : data_(empty_data()), size_(0)
#if FASTTEXT_HAS_MMAP
        , file_descriptor_(-1), mapping_(MAP_FAILED)
#endif
    {}

    ~MappedFile() { close(); }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const std::string& path, std::string* error = NULL) {
        close();
#if FASTTEXT_HAS_MMAP
        file_descriptor_ = ::open(path.c_str(), O_RDONLY);
        if (file_descriptor_ < 0) return set_error(error, "open", path);
        struct stat information;
        if (::fstat(file_descriptor_, &information) != 0) {
            set_error(error, "fstat", path);
            close();
            return false;
        }
        if (information.st_size < 0 ||
            static_cast<std::uintmax_t>(information.st_size) >
                static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
            if (error != NULL) *error = "file size is not representable by size_t: " + path;
            close();
            return false;
        }
        size_ = static_cast<std::size_t>(information.st_size);
        if (size_ == 0) {
            data_ = empty_data();
            ::close(file_descriptor_);
            file_descriptor_ = -1;
            return true;
        }
        mapping_ = ::mmap(NULL, size_, PROT_READ, MAP_PRIVATE, file_descriptor_, 0);
        if (mapping_ == MAP_FAILED) {
            set_error(error, "mmap", path);
            close();
            return false;
        }
        data_ = static_cast<const char*>(mapping_);
        ::close(file_descriptor_);
        file_descriptor_ = -1;
#if defined(MADV_SEQUENTIAL)
        ::madvise(mapping_, size_, MADV_SEQUENTIAL);
#endif
        return true;
#else
        if (!fallback_.load_file(path, error)) return false;
        data_ = fallback_.data();
        size_ = fallback_.size();
        return true;
#endif
    }

    void close() {
#if FASTTEXT_HAS_MMAP
        if (mapping_ != MAP_FAILED) {
            ::munmap(mapping_, size_);
            mapping_ = MAP_FAILED;
        }
        if (file_descriptor_ >= 0) {
            ::close(file_descriptor_);
            file_descriptor_ = -1;
        }
#endif
        data_ = empty_data();
        size_ = 0;
#if !FASTTEXT_HAS_MMAP
        fallback_.clear();
#endif
    }

    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
    StringView view() const { return StringView(data_, size_, 0); }

private:
    static const char* empty_data() {
        static const char value = '\0';
        return &value;
    }

    static bool set_error(std::string* error, const char* operation, const std::string& path) {
        if (error != NULL)
            *error = std::string(operation) + " failed for " + path + ": " + std::strerror(errno);
        return false;
    }

    const char* data_;
    std::size_t size_;
#if FASTTEXT_HAS_MMAP
    int file_descriptor_;
    void* mapping_;
#else
    ReadBuffer fallback_;
#endif
};

struct Line {
    StringView text;
    std::size_t number;
    std::size_t begin_offset;
    std::size_t end_offset;
    std::size_t next_offset;

    Line() : number(0), begin_offset(0), end_offset(0), next_offset(0) {}
};

struct LineWindow {
    bool has_previous;
    bool has_next;
    Line previous;
    Line current;
    Line next;

    LineWindow() : has_previous(false), has_next(false) {}
};

class TextParser {
public:
    TextParser(const char* data, std::size_t size)
        : data_(data), size_(data == NULL ? 0 : size), pending_valid_(false), previous_valid_(false) {
        reset();
    }
    explicit TextParser(const ReadBuffer& buffer)
        : data_(buffer.data()), size_(buffer.size()), pending_valid_(false), previous_valid_(false) {
        reset();
    }
    explicit TextParser(const MappedFile& mapped)
        : data_(mapped.data()), size_(mapped.size()), pending_valid_(false), previous_valid_(false) {
        reset();
    }

    void reset() {
        previous_valid_ = false;
        pending_valid_ = size_ != 0 && make_line(0, 1, &pending_line_);
    }

    bool seek(std::size_t byte_offset) {
        if (size_ == 0 || byte_offset >= size_) {
            pending_valid_ = false;
            previous_valid_ = false;
            return false;
        }
        std::size_t line_start = byte_offset;
        // A saved begin_offset/next_offset always wins. This preserves empty
        // lines whose first (and only) byte is '\n'. For an offset inside a
        // non-empty line, walk back to its preceding newline.
        if (line_start != 0 && data_[line_start - 1] != '\n') {
            while (line_start != 0 && data_[line_start - 1] != '\n') --line_start;
        }
        std::size_t number = 1;
        const char* cursor = data_;
        const char* const stop = data_ + line_start;
        while (cursor < stop) {
            const void* newline = std::memchr(cursor, '\n', static_cast<std::size_t>(stop - cursor));
            if (newline == NULL) break;
            ++number;
            cursor = static_cast<const char*>(newline) + 1;
        }
        pending_valid_ = make_line(line_start, number, &pending_line_);
        previous_valid_ = pending_valid_ && make_previous_line(pending_line_, &previous_line_);
        return pending_valid_;
    }

    bool next(LineWindow* output) {
        if (output == NULL || !pending_valid_) return false;
        LineWindow result;
        result.current = pending_line_;
        result.has_previous = previous_valid_;
        if (previous_valid_) result.previous = previous_line_;
        result.has_next = pending_line_.next_offset < size_ &&
                          make_line(pending_line_.next_offset, pending_line_.number + 1, &result.next);

        previous_line_ = pending_line_;
        previous_valid_ = true;
        if (result.has_next) pending_line_ = result.next;
        pending_valid_ = result.has_next;
        *output = result;
        return true;
    }

private:
    bool make_line(std::size_t start, std::size_t number, Line* output) const {
        if (output == NULL || start >= size_) return false;
        const char* const first = data_ + start;
        const void* newline = std::memchr(first, '\n', size_ - start);
        const std::size_t raw_end = newline == NULL
            ? size_
            : static_cast<std::size_t>(static_cast<const char*>(newline) - data_);
        std::size_t logical_end = raw_end;
        if (logical_end > start && data_[logical_end - 1] == '\r') --logical_end;
        output->text = StringView(first, logical_end - start, start);
        output->number = number;
        output->begin_offset = start;
        output->end_offset = logical_end;
        output->next_offset = raw_end < size_ ? raw_end + 1 : size_;
        return true;
    }

    bool make_previous_line(const Line& current, Line* output) const {
        if (current.begin_offset == 0 || current.number <= 1) return false;
        std::size_t cursor = current.begin_offset - 1;
        while (cursor != 0 && data_[cursor - 1] != '\n') --cursor;
        return make_line(cursor, current.number - 1, output);
    }

    const char* data_;
    std::size_t size_;
    Line pending_line_;
    Line previous_line_;
    bool pending_valid_;
    bool previous_valid_;
};

}  // namespace fasttext

#endif  // FAST_TEXT_PARSER_HPP
