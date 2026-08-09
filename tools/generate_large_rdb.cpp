#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint32_t parsePositiveCount(const char* text, const char* name) {
    char* end = 0;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (!text[0] || *end != '\0' || value == 0UL ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const std::string outputPath = argc > 1
            ? argv[1]
            : "stress_detail_sample.rdb";
        const std::uint32_t checkCount = argc > 2
            ? parsePositiveCount(argv[2], "check count")
            : 6U;
        const std::uint32_t resultsPerCheck = argc > 3
            ? parsePositiveCount(argv[3], "result count")
            : 200000U;

        std::vector<char> outputBuffer(8U * 1024U * 1024U);
        std::ofstream output;
        output.rdbuf()->pubsetbuf(outputBuffer.data(), outputBuffer.size());
        output.open(outputPath.c_str(), std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create output RDB file");
        }

        output << "STRESS_TOP 1000\n";
        for (std::uint32_t check = 1; check <= checkCount; ++check) {
            const bool polygon = (check % 2U) != 0U;
            output << "STRESS."
                   << (polygon ? "POLYGON." : "EDGE.")
                   << check << '\n'
                   << resultsPerCheck << ' ' << resultsPerCheck
                   << " 2 Aug 04 21:30:00 2026\n"
                   << "Synthetic large RDB check " << check << ".\n"
                   << "Generated to demonstrate 10,000-row detail batches.\n";

            for (std::uint32_t result = 1;
                 result <= resultsPerCheck;
                 ++result) {
                const std::int64_t x =
                    static_cast<std::int64_t>(check) * 100000000LL +
                    static_cast<std::int64_t>(result % 1000U) * 10000LL;
                const std::int64_t y =
                    static_cast<std::int64_t>(result / 1000U) * 10000LL;

                if (polygon) {
                    output << "p " << result << " 4\n";
                } else {
                    output << "e " << result << " 2\n";
                }

                output << "PP stress marker check " << check
                       << " result " << result << '\n'
                       << "PA " << 12000000U + result << '\n'
                       << "CN CELL_" << check << '_'
                       << result % 4096U
                       << " c 1 0 0 1 " << x << ' ' << y << '\n';

                if (polygon) {
                    output << x << ' ' << y << '\n'
                           << x + 4000LL << ' ' << y << '\n'
                           << x + 4000LL << ' ' << y + 3000LL << '\n'
                           << x << ' ' << y + 3000LL << '\n';
                } else {
                    output << x << ' ' << y << ' '
                           << x + 4000LL << ' ' << y << '\n'
                           << x + 4000LL << ' ' << y << ' '
                           << x + 4000LL << ' ' << y + 3000LL << '\n';
                }
            }

            if (!output) throw std::runtime_error("failed while writing RDB data");
            std::cout << "generated check " << check << '/' << checkCount
                      << " (" << resultsPerCheck << " results)" << std::endl;
        }

        output.close();
        if (!output) throw std::runtime_error("failed to finalize RDB data");

        std::cout << "created " << outputPath << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "generate_large_rdb: " << error.what() << std::endl;
        return 1;
    }
}
