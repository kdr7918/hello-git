#include "fast_text_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

using fasttext::Cursor;
using fasttext::LineWindow;
using fasttext::MappedFile;
using fasttext::Match;
using fasttext::ReadBuffer;
using fasttext::StringView;
using fasttext::TextParser;

namespace {

int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr << '\n'; \
        ++failures; \
    } \
} while (0)

void check_eq(const StringView& view, const char* expected) {
    CHECK(view == StringView(expected));
}

void test_line_window_crlf_offsets_and_seek() {
    const std::string input = "first\r\nsecond 42 alpha\nthird";
    TextParser parser(input.data(), input.size());
    LineWindow window;

    CHECK(parser.next(&window));
    CHECK(!window.has_previous);
    CHECK(window.has_next);
    check_eq(window.current.text, "first");
    check_eq(window.next.text, "second 42 alpha");
    CHECK(window.current.begin_offset == 0u);
    CHECK(window.current.end_offset == 5u);
    CHECK(window.current.next_offset == 7u);
    CHECK(window.current.number == 1u);

    CHECK(parser.next(&window));
    CHECK(window.has_previous && window.has_next);
    check_eq(window.previous.text, "first");
    check_eq(window.current.text, "second 42 alpha");
    check_eq(window.next.text, "third");
    CHECK(window.current.begin_offset == 7u);
    CHECK(window.current.next_offset == 23u);
    CHECK(window.current.number == 2u);

    CHECK(parser.seek(25u));
    CHECK(parser.next(&window));
    check_eq(window.current.text, "third");
    check_eq(window.previous.text, "second 42 alpha");
    CHECK(window.current.begin_offset == 23u);
    CHECK(window.current.number == 3u);
    CHECK(!window.has_next);

    CHECK(parser.seek(7u));
    CHECK(parser.next(&window));
    check_eq(window.current.text, "second 42 alpha");
    CHECK(window.current.number == 2u);
}

void test_empty_lines_and_final_newline() {
    const std::string input = "\nA\n";
    TextParser parser(input.data(), input.size());
    LineWindow window;
    CHECK(parser.next(&window));
    CHECK(window.current.text.empty());
    CHECK(parser.next(&window));
    check_eq(window.current.text, "A");
    CHECK(!parser.next(&window));
}

void test_seek_roundtrip_for_consecutive_empty_lines() {
    const std::string input = "a\n\nb";
    TextParser parser(input.data(), input.size());
    LineWindow window;

    CHECK(parser.next(&window));
    CHECK(window.has_next);
    CHECK(window.next.begin_offset == 2u);
    CHECK(window.next.text.empty());

    CHECK(parser.seek(window.next.begin_offset));
    CHECK(parser.next(&window));
    CHECK(window.current.text.empty());
    CHECK(window.current.number == 2u);
    check_eq(window.previous.text, "a");
    check_eq(window.next.text, "b");

    CHECK(parser.seek(3u));
    CHECK(parser.next(&window));
    check_eq(window.current.text, "b");
    CHECK(window.has_previous);
    CHECK(window.previous.text.empty());
    CHECK(window.previous.begin_offset == 2u);
}

void test_null_memory_range_is_empty() {
    TextParser parser(NULL, 10u);
    LineWindow window;
    CHECK(!parser.next(&window));
}

void test_cursor_numbers_alpha_and_words() {
    const std::string input = "  -9223372036854775808 18446744073709551615 Alpha_42 3.125e2";
    Cursor cursor((StringView(input)));
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    double double_value = 0.0;

    cursor.skip_spaces();
    CHECK(cursor.read_int64(&signed_value));
    CHECK(signed_value == INT64_MIN);
    cursor.skip_spaces();
    CHECK(cursor.read_uint64(&unsigned_value));
    CHECK(unsigned_value == UINT64_MAX);
    cursor.skip_spaces();
    check_eq(cursor.read_alpha(), "Alpha");
    check_eq(cursor.read_char('_') ? cursor.read_while_digits() : StringView(), "42");
    cursor.skip_spaces();
    CHECK(cursor.read_double(&double_value));
    CHECK(double_value == 312.5);

    Cursor bad(StringView("18446744073709551616"));
    CHECK(!bad.read_uint64(&unsigned_value));
}

void test_string_view_classification_and_numeric_values() {
    CHECK(StringView("AlphaOnly").is_alpha());
    CHECK(!StringView("").is_alpha());
    CHECK(!StringView("Alpha42").is_alpha());
    CHECK(!StringView("한글").is_alpha());

    CHECK(StringView("-42").is_integer());
    CHECK(StringView("+42").is_integer());
    CHECK(!StringView("42.0").is_integer());
    CHECK(!StringView(" 42").is_integer());
    CHECK(!StringView("+").is_integer());

    CHECK(StringView("42").is_number());
    CHECK(StringView("-3.125e+2").is_number());
    CHECK(StringView(".5").is_number());
    CHECK(!StringView("1e").is_number());
    CHECK(!StringView("12x").is_number());
    CHECK(!StringView("").is_number());

    std::int64_t signed_value = 7;
    std::uint64_t unsigned_value = 7;
    double number_value = 7.0;
    CHECK(StringView("-9223372036854775808").to_int64(&signed_value));
    CHECK(signed_value == INT64_MIN);
    CHECK(StringView("18446744073709551615").to_uint64(&unsigned_value));
    CHECK(unsigned_value == UINT64_MAX);
    CHECK(StringView("-3.125e2").to_double(&number_value));
    CHECK(number_value == -312.5);

    unsigned_value = 99;
    CHECK(!StringView("-1").to_uint64(&unsigned_value));
    CHECK(unsigned_value == 99u);

    number_value = 99.0;
    CHECK(!StringView("1e9999").to_double(&number_value));
    CHECK(number_value == 99.0);
    CHECK(!StringView("1e-9999").to_double(&number_value));
    CHECK(number_value == 99.0);
    CHECK(StringView("0e-9999").to_double(&number_value));
    CHECK(number_value == 0.0);

    const char embedded_nul[] = {'1', '2', '\0', '3'};
    CHECK(!StringView(embedded_nul, sizeof(embedded_nul)).is_number());

    signed_value = 99;
    CHECK(!StringView("9223372036854775808").to_int64(&signed_value));
    CHECK(signed_value == 99);
    CHECK(StringView("-42").int64_value() == -42);
    CHECK(StringView("bad").int64_value(123) == 123);
    CHECK(StringView("42").uint64_value() == 42u);
    CHECK(StringView("-1").uint64_value(123u) == 123u);
    CHECK(StringView("2.5").number_value() == 2.5);
    CHECK(StringView("bad").number_value(1.25) == 1.25);

    const StringView invalid(NULL, 5u);
    CHECK(!invalid.is_alpha());
    CHECK(!invalid.is_integer());
    CHECK(!invalid.is_number());

    const char* current_locale = std::setlocale(LC_NUMERIC, NULL);
    const std::string saved_locale = current_locale == NULL ? "C" : current_locale;
    const char* changed = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
    if (changed == NULL) changed = std::setlocale(LC_NUMERIC, "de_DE.utf8");
    if (changed != NULL) {
        number_value = 0.0;
        CHECK(StringView("1.5").to_double(&number_value));
        CHECK(number_value == 1.5);
        CHECK(StringView("1.5").is_number());
    }
    CHECK(std::setlocale(LC_NUMERIC, saved_locale.c_str()) != NULL);
}

void test_split_find_word_and_regex() {
    const StringView line("alpha,beta,,alphabet alpha42 alpha ERROR-123");
    std::vector<StringView> parts = fasttext::split(line, ',');
    CHECK(parts.size() == 4u);
    check_eq(parts[0], "alpha");
    check_eq(parts[1], "beta");
    CHECK(parts[2].empty());
    check_eq(parts[3], "alphabet alpha42 alpha ERROR-123");

    CHECK(fasttext::find_char(line, 'b') == 6u);
    CHECK(fasttext::find(line, StringView("ERROR")) == 35u);
    CHECK(fasttext::contains_word(line, StringView("alpha")));
    CHECK(!fasttext::contains_word(line, StringView("alp")));
    CHECK(!fasttext::contains_word(StringView("alpha42"), StringView("alpha")));

    const std::regex expression("ERROR-([0-9]+)", std::regex::ECMAScript | std::regex::optimize);
    Match match;
    CHECK(fasttext::regex_find(line, expression, &match));
    CHECK(match.offset == 35u);
    CHECK(match.length == 9u);
}

void test_trim_and_zero_allocation_split_callback() {
    const StringView input(" \t one | two |three\r ");
    check_eq(fasttext::trim(input), "one | two |three");
    std::size_t count = 0;
    std::string joined;
    fasttext::split_each(fasttext::trim(input), '|', [&](StringView part) {
        if (!joined.empty()) joined += ',';
        joined += fasttext::trim(part).to_string();
        ++count;
    });
    CHECK(count == 3u);
    CHECK(joined == "one,two,three");
}

void test_string_view_whitespace_split_trims_ends_and_collapses_runs() {
    const std::string storage = " \t alpha  beta\r\n gamma \v ";
    const StringView input(storage.data(), storage.size(), 100u);
    const std::vector<StringView> parts = input.split_whitespace();
    CHECK(parts.size() == 3u);
    check_eq(parts[0], "alpha");
    check_eq(parts[1], "beta");
    check_eq(parts[2], "gamma");
    CHECK(parts[0].offset == 103u);
    CHECK(parts[1].offset == 110u);
    CHECK(parts[2].offset == 117u);

    std::size_t count = 0;
    std::string joined;
    fasttext::split_whitespace_each(input, [&](StringView part) {
        if (!joined.empty()) joined += '|';
        joined += part.to_string();
        ++count;
    });
    CHECK(count == 3u);
    CHECK(joined == "alpha|beta|gamma");
    CHECK(StringView("  \t\r\n ").split_whitespace().empty());
    CHECK(StringView("").split_whitespace().empty());
    const std::vector<StringView> form_feed = StringView("left\f\fright").split_whitespace();
    CHECK(form_feed.size() == 2u);
    check_eq(form_feed[0], "left");
    check_eq(form_feed[1], "right");
}

void test_read_buffer_file_and_offset_roundtrip() {
#if defined(__unix__) || defined(__APPLE__)
    char path[] = "/tmp/fast_text_parser_test_XXXXXX";
    const int descriptor = ::mkstemp(path);
    CHECK(descriptor >= 0);
    if (descriptor < 0) return;
    FILE* file = ::fdopen(descriptor, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        ::close(descriptor);
        std::remove(path);
        return;
    }
    const char text[] = "zero\none\ntwo";
    CHECK(std::fwrite(text, 1, sizeof(text) - 1, file) == sizeof(text) - 1);
    CHECK(std::fclose(file) == 0);

    ReadBuffer buffer;
    std::string error;
    CHECK(buffer.load_file(path, &error));
    TextParser parser(buffer);
    LineWindow window;
    CHECK(parser.next(&window));
    const std::size_t saved_offset = window.next.begin_offset;
    CHECK(saved_offset == 5u);
    CHECK(parser.seek(saved_offset));
    CHECK(parser.next(&window));
    check_eq(window.current.text, "one");

    MappedFile mapped;
    CHECK(mapped.open(path, &error));
    TextParser mapped_parser(mapped);
    CHECK(mapped_parser.seek(9u));
    CHECK(mapped_parser.next(&window));
    check_eq(window.current.text, "two");
    mapped.close();
    std::remove(path);
#endif
}

}  // namespace

int main() {
    test_line_window_crlf_offsets_and_seek();
    test_empty_lines_and_final_newline();
    test_seek_roundtrip_for_consecutive_empty_lines();
    test_null_memory_range_is_empty();
    test_cursor_numbers_alpha_and_words();
    test_string_view_classification_and_numeric_values();
    test_split_find_word_and_regex();
    test_trim_and_zero_allocation_split_callback();
    test_string_view_whitespace_split_trims_ends_and_collapses_runs();
    test_read_buffer_file_and_offset_roundtrip();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
