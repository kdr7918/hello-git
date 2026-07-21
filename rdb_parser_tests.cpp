#include "calibre_ascii_rdb_parser.hpp"

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

std::string text(const calibre::rdb::Database& database, calibre::rdb::StringId id) {
    return database.strings.get(id).str();
}

const calibre::rdb::RuleCheck& rule(const calibre::rdb::Database& database, std::size_t index) {
    return database.rule_checks[index];
}

const calibre::rdb::Result& result(const calibre::rdb::Database& database,
                                   const calibre::rdb::RuleCheck& rule_check,
                                   std::size_t index) {
    return database.results[rule_check.results.begin + index];
}

} // namespace

int main() {
    const calibre::rdb::AsciiRdbParser parser;

    const calibre::rdb::Database standard = parser.parse_file(sample_path("standard_sample.rdb"));
    RDB_CHECK(text(standard, standard.top_cell_name) == "TOP_CHIP");
    RDB_CHECK(standard.database_precision == 1000);
    RDB_CHECK(standard.rule_checks.size() == 3);
    RDB_CHECK(text(standard, rule(standard, 0).name) == "M1.SPACING.1");
    RDB_CHECK(rule(standard, 0).check_text.count == 3);
    RDB_CHECK(rule(standard, 0).results.count == 2);
    RDB_CHECK(result(standard, rule(standard, 0), 0).kind == calibre::rdb::ResultKind::Polygon);
    RDB_CHECK(result(standard, rule(standard, 0), 0).geometry.count == 4);
    RDB_CHECK(result(standard, rule(standard, 0), 0).properties_before_geometry.count == 5);
    RDB_CHECK(result(standard, rule(standard, 0), 1).kind == calibre::rdb::ResultKind::EdgeCluster);
    RDB_CHECK(result(standard, rule(standard, 0), 1).geometry.count == 2);
    RDB_CHECK(result(standard, rule(standard, 1), 0).properties_before_geometry.count == 2);
    RDB_CHECK(rule(standard, 2).results.empty());

    const calibre::rdb::Database post_geometry =
        parser.parse_file(sample_path("post_coordinate_tags_sample.rdb"));
    RDB_CHECK(post_geometry.rule_checks.size() == 1);
    RDB_CHECK(rule(post_geometry, 0).results.count == 2);
    RDB_CHECK(result(post_geometry, rule(post_geometry, 0), 0).properties_after_geometry.count == 2);
    RDB_CHECK(result(post_geometry, rule(post_geometry, 0), 1).properties_after_geometry.count == 3);

    const calibre::rdb::Database large_standard =
        parser.parse_file(sample_path("large_standard_sample.rdb"));
    RDB_CHECK(large_standard.rule_checks.size() == 100);
    RDB_CHECK(large_standard.results.size() == 200);
    RDB_CHECK(large_standard.vertices.size() == 400);
    RDB_CHECK(large_standard.edges.size() == 200);

    const calibre::rdb::Database large_post_geometry =
        parser.parse_file(sample_path("large_post_coordinate_tags_sample.rdb"));
    RDB_CHECK(large_post_geometry.rule_checks.size() == 100);
    RDB_CHECK(large_post_geometry.results.size() == 200);
    RDB_CHECK(large_post_geometry.vertices.size() == 400);
    RDB_CHECK(large_post_geometry.edges.size() == 200);

    std::cout << "rdb-parser-tests: OK\n";
}
