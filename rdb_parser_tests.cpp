#include "ascii_rdb_parser.hpp"
#include "rdb_check_detail.hpp"
#include "rdb_check_geometry.hpp"
#include "rdb_check_index.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
