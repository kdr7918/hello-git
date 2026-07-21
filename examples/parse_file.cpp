#include "fast_text_parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " FILE\n";
        return EXIT_FAILURE;
    }

    fasttext::MappedFile file;
    std::string error;
    if (!file.open(argv[1], &error)) {
        std::cerr << error << '\n';
        return EXIT_FAILURE;
    }

    fasttext::TextParser parser(file);
    fasttext::LineWindow lines;
    while (parser.next(&lines)) {
        // 모든 StringView는 복사 없는 [pointer, length] 뷰입니다.
        if (fasttext::contains_word(lines.current.text, fasttext::StringView("ERROR"))) {
            std::cout << "line=" << lines.current.number
                      << " begin=" << lines.current.begin_offset
                      << " next=" << lines.current.next_offset
                      << " text=" << lines.current.text.to_string() << '\n';

            if (lines.has_previous)
                std::cout << "  previous: " << lines.previous.text.to_string() << '\n';
            if (lines.has_next)
                std::cout << "  next: " << lines.next.text.to_string() << '\n';
        }
    }
    return EXIT_SUCCESS;
}
