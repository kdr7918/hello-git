#include "fast_text_parser.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <vector>

int main() {
    static const char text[] = "  42 Alpha_42 status=ok ERROR-123 next_value  ";
    const fasttext::StringView line(
        text,
        sizeof(text) - 1,
        1000u);  // 예: 파일에서 시작 offset이 1000인 줄

    std::cout << "[split_whitespace]\n";
    const std::vector<fasttext::StringView> words = line.split_whitespace();
    for (std::size_t index = 0; index < words.size(); ++index) {
        std::cout << index << ": " << words[index].to_string()
                  << " (absolute offset=" << words[index].offset << ")\n";
    }

    std::cout << "\n[Cursor]\n";
    fasttext::Cursor cursor(fasttext::trim(line));
    std::int64_t id = 0;
    if (!cursor.read_int64(&id)) return EXIT_FAILURE;
    cursor.skip_spaces();
    const fasttext::StringView name = cursor.read_identifier();
    cursor.skip_spaces();
    const fasttext::StringView status = cursor.read_word();

    std::cout << "id=" << id << '\n';
    std::cout << "name=" << name.to_string()
              << " offset=" << name.offset << '\n';
    std::cout << "status=" << status.to_string()
              << " cursor absolute offset=" << cursor.absolute_offset() << '\n';

    std::cout << "\n[Match / regex_find]\n";
    const std::regex expression(
        "ERROR-([0-9]+)",
        std::regex::ECMAScript | std::regex::optimize);
    fasttext::Match match;
    if (fasttext::regex_find(line, expression, &match)) {
        const std::size_t relative_offset = match.offset - line.offset;
        const fasttext::StringView matched = line.substr(relative_offset, match.length);
        std::cout << "matched=" << matched.to_string() << '\n';
        std::cout << "absolute offset=" << match.offset
                  << " length=" << match.length << '\n';
    }

    return EXIT_SUCCESS;
}
