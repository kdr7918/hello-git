#include "ascii_rdb_parser.hpp"
#include "rdb_check_detail.hpp"
#include "rdb_check_geometry.hpp"
#include "rdb_check_index.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>

namespace {

void check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "check failed: " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define RDB_CHECK(expression) check((expression), #expression)

std::string sample_path(const char* name) {
    return std::string(RDB_SAMPLE_DIR) + "/" + name;
}

std::string text(const rdb::Database& database, rdb::StringId id) {
    return database.strings.get(id).str();
}

class TemporaryRdb {
public:
    explicit TemporaryRdb(const std::string& contents) {
        char pattern[] = "/tmp/rdb-parser-test-XXXXXX";
        const int fd = ::mkstemp(pattern);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = pattern;
        std::size_t written = 0;
        while (written < contents.size()) {
            const ssize_t count = ::write(fd, contents.data() + written, contents.size() - written);
            if (count <= 0) {
                ::close(fd);
                ::unlink(path_.c_str());
                throw std::runtime_error("temporary RDB write failed");
            }
            written += static_cast<std::size_t>(count);
        }
        if (::close(fd) != 0) {
            ::unlink(path_.c_str());
            throw std::runtime_error("temporary RDB close failed");
        }
    }

    ~TemporaryRdb() { ::unlink(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    TemporaryRdb(const TemporaryRdb&);
    TemporaryRdb& operator=(const TemporaryRdb&);
    std::string path_;
};

const rdb::RuleCheck& rule(const rdb::Database& database, std::size_t index) {
    return database.rule_checks[index];
}

const rdb::Result& result(const rdb::Database& database,
                          const rdb::RuleCheck& rule_check,
                                   std::size_t index) {
    return database.results[rule_check.results.begin + index];
}

} // namespace

int main() {
    const rdb::AsciiRdbParser parser;

    const rdb::Database standard = parser.parse_file(sample_path("standard_sample.rdb"));
    RDB_CHECK(text(standard, standard.top_cell_name) == "TOP_CHIP");
    RDB_CHECK(standard.database_precision == 1000);
    RDB_CHECK(standard.rule_checks.size() == 3);
    RDB_CHECK(text(standard, rule(standard, 0).name) == "M1.SPACING.1");
    RDB_CHECK(rule(standard, 0).check_text.count == 3);
    RDB_CHECK(rule(standard, 0).results.count == 2);
    RDB_CHECK(result(standard, rule(standard, 0), 0).kind == rdb::ResultKind::Polygon);
    RDB_CHECK(result(standard, rule(standard, 0), 0).geometry.count == 4);
    RDB_CHECK(result(standard, rule(standard, 0), 0).properties_before_geometry.count == 5);
    RDB_CHECK(text(standard, standard.tagged_values[
        result(standard, rule(standard, 0), 0).properties_before_geometry.begin].id) == "PP");
    RDB_CHECK(text(standard, standard.tagged_values[
        result(standard, rule(standard, 0), 0).properties_before_geometry.begin].payload) == "M1 spacing marker");
    RDB_CHECK(standard.vertices[0].x == 10000);
    RDB_CHECK(standard.vertices[0].y == 20000);
    RDB_CHECK(result(standard, rule(standard, 0), 1).kind == rdb::ResultKind::EdgeCluster);
    RDB_CHECK(result(standard, rule(standard, 0), 1).geometry.count == 2);
    RDB_CHECK(result(standard, rule(standard, 1), 0).properties_before_geometry.count == 2);
    RDB_CHECK(rule(standard, 2).results.empty());

    const rdb::Database post_geometry =
        parser.parse_file(sample_path("post_coordinate_tags_sample.rdb"));
    RDB_CHECK(post_geometry.rule_checks.size() == 1);
    RDB_CHECK(rule(post_geometry, 0).results.count == 2);
    RDB_CHECK(result(post_geometry, rule(post_geometry, 0), 0).properties_after_geometry.count == 2);
    RDB_CHECK(result(post_geometry, rule(post_geometry, 0), 1).properties_after_geometry.count == 3);

    const rdb::Database large_standard =
        parser.parse_file(sample_path("large_standard_sample.rdb"));
    RDB_CHECK(large_standard.rule_checks.size() == 100);
    RDB_CHECK(large_standard.results.size() == 200);
    RDB_CHECK(large_standard.vertices.size() == 400);
    RDB_CHECK(large_standard.edges.size() == 200);

    const rdb::Database large_post_geometry =
        parser.parse_file(sample_path("large_post_coordinate_tags_sample.rdb"));
    RDB_CHECK(large_post_geometry.rule_checks.size() == 100);
    RDB_CHECK(large_post_geometry.results.size() == 200);
    RDB_CHECK(large_post_geometry.vertices.size() == 400);
    RDB_CHECK(large_post_geometry.edges.size() == 200);

    const rdb::FastCheckIndexParser index_parser;
    const rdb::CheckIndexDatabase index_database =
        index_parser.parse_database(sample_path("standard_sample.rdb"));
    static_assert(
        std::is_same<decltype(index_database.database_precision), double>::value,
        "fast index database precision must be double");
    RDB_CHECK(index_database.top_cell_name == "TOP_CHIP");
    RDB_CHECK(index_database.database_precision == 1000.0);
    RDB_CHECK(index_database.checks.size() == 3);
    RDB_CHECK(index_database.checks[0].name == "M1.SPACING.1");

    const std::vector<rdb::CheckIndexEntry> index =
        index_parser.parse_file(sample_path("standard_sample.rdb"));
    RDB_CHECK(index.size() == 3);
    RDB_CHECK(index[0].name == "M1.SPACING.1");
    RDB_CHECK(index[0].offset > 0);
    RDB_CHECK(index[0].geometry_count == 2);
    RDB_CHECK(index[1].geometry_count == 1);
    RDB_CHECK(index[2].geometry_count == 0);

    rdb::FastCheckIndexOptions small_index_options;
    small_index_options.read_buffer_bytes = 71;
    small_index_options.context_bytes = 64;
    const std::vector<rdb::CheckIndexEntry> small_buffer_index =
        index_parser.parse_file(sample_path("standard_sample.rdb"), small_index_options);
    RDB_CHECK(small_buffer_index.size() == 3);
    RDB_CHECK(small_buffer_index[0].name == "M1.SPACING.1");
    RDB_CHECK(small_buffer_index[2].offset == index[2].offset);
    RDB_CHECK(index_parser.parse_file(sample_path("large_standard_sample.rdb")).size() == 100);
    RDB_CHECK(index_parser.parse_file(sample_path("large_post_coordinate_tags_sample.rdb")).size() == 100);

    // Top-cell header가 read() 버퍼 경계를 넘어도 이름과 precision을 온전히 복원한다.
    const std::string long_top_cell(180, 'T');
    const TemporaryRdb long_header_file(
        long_top_cell + " 0.0005\nLONG.HEADER.CHECK\n1 1 0 Jul 21 12:10:49 2026\n");
    const rdb::CheckIndexDatabase long_header_index =
        index_parser.parse_database(long_header_file.path(), small_index_options);
    RDB_CHECK(long_header_index.top_cell_name == long_top_cell);
    RDB_CHECK(long_header_index.database_precision == 0.0005);
    RDB_CHECK(long_header_index.checks.size() == 1);

    // Full parser와 동일하게 선행 blank 줄과 여러 단어 Top-cell 이름을 허용한다.
    const TemporaryRdb blank_header_file(
        "\n \r\nTOP CELL BLOCK 1.25e-3\r\nBLANK.HEADER.CHECK\r\n"
        "1 1 0 Jul 21 12:10:49 2026\r\n");
    const rdb::CheckIndexDatabase blank_header_index =
        index_parser.parse_database(blank_header_file.path());
    RDB_CHECK(blank_header_index.top_cell_name == "TOP CELL BLOCK");
    RDB_CHECK(blank_header_index.database_precision == 1.25e-3);
    RDB_CHECK(blank_header_index.checks.size() == 1);

    // Top-cell/precision을 반환하는 API는 잘못된 첫 nonblank header를 거부한다.
    const TemporaryRdb invalid_header_file(
        "INVALID_HEADER\nCHECK\n1 1 0 Jul 21 12:10:49 2026\n");
    bool invalid_header_rejected = false;
    try {
        index_parser.parse_database(invalid_header_file.path());
    } catch (const rdb::ScanError&) {
        invalid_header_rejected = true;
    }
    RDB_CHECK(invalid_header_rejected);

    // Precision은 유한한 양수 double만 허용한다.
    const char* const invalid_precisions[] = {
        "0", "-0.001", "nan", "inf", "1e309", "1.0junk"
    };
    for (std::size_t i = 0;
         i < sizeof(invalid_precisions) / sizeof(invalid_precisions[0]);
         ++i) {
        const TemporaryRdb invalid_precision_file(
            std::string("TOP ") + invalid_precisions[i] + "\n");
        bool invalid_precision_rejected = false;
        try {
            index_parser.parse_database(invalid_precision_file.path());
        } catch (const rdb::ScanError&) {
            invalid_precision_rejected = true;
        }
        RDB_CHECK(invalid_precision_rejected);
    }

    // Check 이름 시작점이 read() overlap의 첫 바이트와 정확히 겹쳐도 누락되면 안 된다.
    const std::string boundary_name(50, 'N');
    const std::string boundary_contents =
        std::string("TOP 1000\nPAD\n") + boundary_name + "\n7 7 0 Jul 21 12:10:49 2026\n";
    const TemporaryRdb boundary_file(boundary_contents);
    rdb::FastCheckIndexOptions boundary_options;
    boundary_options.read_buffer_bytes = 71;
    boundary_options.context_bytes = 64;
    const std::vector<rdb::CheckIndexEntry> boundary_index =
        index_parser.parse_file(boundary_file.path(), boundary_options);
    RDB_CHECK(boundary_index.size() == 1);
    RDB_CHECK(boundary_index[0].name == boundary_name);
    RDB_CHECK(boundary_index[0].offset == 13);
    RDB_CHECK(boundary_index[0].geometry_count == 7);

    // 모든 작은 prefix 배치에서 Check 이름/시간이 read() 경계를 넘어도 정확히 한 번 검출한다.
    for (std::size_t prefix_size = 8; prefix_size < 96; ++prefix_size) {
        const std::string prefix(prefix_size, 'P');
        const TemporaryRdb shifted_file(
            "TOP 1000\n" + prefix + "\n" + boundary_name +
            "\n7 7 0 Jul 21 12:10:49 2026\n");
        const std::vector<rdb::CheckIndexEntry> shifted_index =
            index_parser.parse_file(shifted_file.path(), boundary_options);
        RDB_CHECK(shifted_index.size() == 1);
        RDB_CHECK(shifted_index[0].offset == 9U + prefix_size + 1U);
    }

    // HH:MM:SS 앞에도 token 경계가 있어야 하며, 식별자 일부의 시간 모양은 제외한다.
    const TemporaryRdb embedded_time_file(
        "TOP 1000\nNOT_A_CHECK\n7 7 0 Jul 21 x12:10:49 2026\n");
    RDB_CHECK(index_parser.parse_file(embedded_time_file.path()).empty());

    const rdb::CheckDetailParser detail_parser;
    const rdb::CheckDetail first_check =
        detail_parser.parse_file_at(sample_path("standard_sample.rdb"), index[0].offset);
    RDB_CHECK(first_check.name == "M1.SPACING.1");
    RDB_CHECK(first_check.results.size() == 2);
    RDB_CHECK(first_check.results[0].vertices.size() == 4);
    RDB_CHECK(first_check.results[0].properties_before_geometry.size() == 5);
    RDB_CHECK(first_check.results[1].edges.size() == 2);

    const rdb::CheckDetailFile detail_file(sample_path("standard_sample.rdb"));
    const rdb::CheckDetail second_check = detail_file.parse_at(index[1].offset);
    RDB_CHECK(second_check.name == "M2.DENSITY.MIN");
    RDB_CHECK(second_check.results.size() == 1);
    RDB_CHECK(second_check.results[0].vertices.size() == 4);

    const rdb::CheckDetail extended_check = detail_parser.parse_file_at(
        sample_path("post_coordinate_tags_sample.rdb"),
        index_parser.parse_file(sample_path("post_coordinate_tags_sample.rdb"))[0].offset);
    RDB_CHECK(extended_check.results[0].properties_after_geometry.size() == 2);
    RDB_CHECK(extended_check.results[1].properties_after_geometry.size() == 3);

    const rdb::CheckGeometryParser geometry_parser;
    const rdb::GeometryDatabase geometry = geometry_parser.parse_file(sample_path("standard_sample.rdb"));
    RDB_CHECK(geometry.checks.size() == 3);
    RDB_CHECK(geometry.results.size() == 3);
    RDB_CHECK(geometry.vertices.size() == 8);
    RDB_CHECK(geometry.edges.size() == 2);
    RDB_CHECK(geometry.vertices[0].x == 10000);
    RDB_CHECK(geometry.vertices[0].y == 20000);
    RDB_CHECK(geometry.edges[0].second.x == 33000);
    RDB_CHECK(geometry.edges[0].second.y == 20000);
    RDB_CHECK(geometry.checks[0].results.count == 2);
    RDB_CHECK(geometry.checks[1].results.count == 1);

    const rdb::GeometryDatabase large_geometry =
        geometry_parser.parse_file(sample_path("large_post_coordinate_tags_sample.rdb"));
    RDB_CHECK(large_geometry.checks.size() == 100);
    RDB_CHECK(large_geometry.results.size() == 200);
    RDB_CHECK(large_geometry.vertices.size() == 400);
    RDB_CHECK(large_geometry.edges.size() == 200);

    std::cout << "rdb-parser-tests: OK\n";
}
