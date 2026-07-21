#include "fast_text_parser.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

typedef std::chrono::steady_clock Clock;
volatile std::size_t benchmark_sink = 0;

struct Result {
    const char* name;
    double seconds;
    std::size_t lines;
    std::size_t checksum;
};

Result scan_mmap(const std::string& path) {
    fasttext::MappedFile mapped;
    std::string error;
    const Clock::time_point begin = Clock::now();
    if (!mapped.open(path, &error)) {
        std::cerr << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    fasttext::TextParser parser(mapped);
    fasttext::LineWindow window;
    std::size_t lines = 0;
    std::size_t checksum = 0;
    while (parser.next(&window)) {
        ++lines;
        checksum += window.current.text.size + window.current.begin_offset;
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    benchmark_sink = checksum;
    Result result = {"mmap + memchr + LineWindow", seconds, lines, checksum};
    return result;
}

Result scan_read_buffer(const std::string& path) {
    fasttext::ReadBuffer buffer;
    std::string error;
    const Clock::time_point begin = Clock::now();
    if (!buffer.load_file(path, &error)) {
        std::cerr << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    fasttext::TextParser parser(buffer);
    fasttext::LineWindow window;
    std::size_t lines = 0;
    std::size_t checksum = 0;
    while (parser.next(&window)) {
        ++lines;
        checksum += window.current.text.size + window.current.begin_offset;
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    benchmark_sink = checksum;
    Result result = {"fread ReadBuffer + memchr", seconds, lines, checksum};
    return result;
}

Result scan_getline(const std::string& path) {
    const Clock::time_point begin = Clock::now();
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    std::string line;
    std::size_t lines = 0;
    std::size_t checksum = 0;
    std::size_t offset = 0;
    while (std::getline(input, line)) {
        const std::size_t raw_size = line.size();
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        ++lines;
        checksum += line.size() + offset;
        offset += raw_size + 1;
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    benchmark_sink = checksum;
    Result result = {"std::getline baseline", seconds, lines, checksum};
    return result;
}

std::size_t file_size(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "cannot open benchmark input: " << path << '\n';
        std::exit(EXIT_FAILURE);
    }
    const std::ifstream::pos_type position = input.tellg();
    if (position < 0) {
        std::cerr << "cannot determine benchmark input size: " << path << '\n';
        std::exit(EXIT_FAILURE);
    }
    const std::streamoff value = static_cast<std::streamoff>(position);
    if (static_cast<std::uintmax_t>(value) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "benchmark input is too large for size_t: " << path << '\n';
        std::exit(EXIT_FAILURE);
    }
    return static_cast<std::size_t>(value);
}

void create_input(FILE* output, std::size_t records) {
    const char record[] = "123456789 Alpha_42 beta,gamma status=OK value=-98765\n";
    for (std::size_t index = 0; index < records; ++index) {
        if (std::fwrite(record, 1, sizeof(record) - 1, output) != sizeof(record) - 1) {
            std::perror("fwrite");
            std::exit(EXIT_FAILURE);
        }
    }
    if (std::fclose(output) != 0) {
        std::perror("fclose");
        std::exit(EXIT_FAILURE);
    }
}

void print_result(const Result& result, std::size_t bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    const double seconds = result.seconds > 0.0 ? result.seconds :
                           std::numeric_limits<double>::min();
    std::cout << std::left << std::setw(31) << result.name
              << "  " << std::right << std::fixed << std::setprecision(3)
              << result.seconds << " s  " << std::setprecision(2)
              << (gib / seconds) << " GiB/s  "
              << result.lines << " lines  checksum=" << result.checksum << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool temporary = false;
    if (argc == 2) {
        path = argv[1];
    } else {
#if defined(__unix__) || defined(__APPLE__)
        char path_template[] = "/tmp/cpp11_fast_text_parser_benchmark_XXXXXX";
        const int descriptor = ::mkstemp(path_template);
        if (descriptor < 0) {
            std::perror("mkstemp");
            return EXIT_FAILURE;
        }
        FILE* output = ::fdopen(descriptor, "wb");
        if (output == NULL) {
            std::perror("fdopen");
            ::close(descriptor);
            std::remove(path_template);
            return EXIT_FAILURE;
        }
        path = path_template;
        create_input(output, 2000000u);
        temporary = true;
#else
        std::cerr << "usage: " << argv[0] << " INPUT_FILE\n";
        return EXIT_FAILURE;
#endif
    }

    const std::size_t bytes = file_size(path);
    std::cout << "input: " << path << " (" << bytes << " bytes)\n";

    // 한 번 예열한 뒤 동일한 warm-cache 조건에서 비교합니다.
    const Result warmup = scan_mmap(path);
    benchmark_sink = warmup.checksum;
    const Result mapped = scan_mmap(path);
    const Result buffered = scan_read_buffer(path);
    const Result getline_result = scan_getline(path);

    if (mapped.lines != buffered.lines || mapped.lines != getline_result.lines ||
        mapped.checksum != buffered.checksum || mapped.checksum != getline_result.checksum) {
        std::cerr << "benchmark implementations produced different results\n";
        return EXIT_FAILURE;
    }

    print_result(mapped, bytes);
    print_result(buffered, bytes);
    print_result(getline_result, bytes);
    if (temporary) std::remove(path.c_str());
    return EXIT_SUCCESS;
}
