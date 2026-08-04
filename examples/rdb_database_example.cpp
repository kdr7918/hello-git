#include "rdb_indexed_file.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TraversalTotals {
    std::size_t polygons;
    std::size_t edge_clusters;
    std::size_t properties;
    std::size_t vertices;
    std::size_t edges;
    std::size_t text_bytes;
    double coordinate_sum;

    TraversalTotals()
        : polygons(0), edge_clusters(0), properties(0), vertices(0), edges(0),
          text_bytes(0), coordinate_sum(0.0) {}
};

std::string text(const rdb::Database& database, rdb::StringId id) {
    return database.strings.get(id).str();
}

// Check의 모든 Result에서 Tagged key(StringId)만 빠르게 모은다.
// 파일의 등장 순서를 유지하고 중복 key도 유지하며, 문자열 바이트는 복사하지 않는다.
std::vector<rdb::StringId> tagged_keys(
    const rdb::Database& database,
    const rdb::RuleCheck& check) {
    std::size_t property_count = 0;
    for (rdb::Index i = 0; i < check.results.count; ++i) {
        const rdb::Result& result = database.results[check.results.begin + i];
        if (result.properties.count >
            std::numeric_limits<std::size_t>::max() - property_count) {
            throw std::length_error("tagged key count exceeds size_t capacity");
        }
        property_count += result.properties.count;
    }

    std::vector<rdb::StringId> keys;
    keys.reserve(property_count);
    for (rdb::Index i = 0; i < check.results.count; ++i) {
        const rdb::Result& result = database.results[check.results.begin + i];
        for (rdb::Index j = 0; j < result.properties.count; ++j) {
            keys.push_back(database.tagged_values[result.properties.begin + j].id);
        }
    }
    return keys;
}

TraversalTotals traverse(const rdb::Database& database, const rdb::RuleCheck& check) {
    TraversalTotals totals;
    for (rdb::Index i = 0; i < check.results.count; ++i) {
        const rdb::Result& result = database.results[check.results.begin + i];
        for (rdb::Index j = 0; j < result.properties.count; ++j) {
            const rdb::TaggedValue& property =
                database.tagged_values[result.properties.begin + j];
            totals.text_bytes += database.strings.get(property.id).size;
            totals.text_bytes += database.strings.get(property.payload).size;
            ++totals.properties;
        }
        if (result.kind == rdb::ResultKind::Polygon) {
            ++totals.polygons;
            for (rdb::Index j = 0; j < result.geometry.count; ++j) {
                const rdb::Point& point = database.vertices[result.geometry.begin + j];
                totals.coordinate_sum += static_cast<double>(point.x);
                totals.coordinate_sum += static_cast<double>(point.y);
                ++totals.vertices;
            }
        } else {
            ++totals.edge_clusters;
            for (rdb::Index j = 0; j < result.geometry.count; ++j) {
                const rdb::Edge& edge = database.edges[result.geometry.begin + j];
                totals.coordinate_sum += static_cast<double>(edge.first.x);
                totals.coordinate_sum += static_cast<double>(edge.first.y);
                totals.coordinate_sum += static_cast<double>(edge.second.x);
                totals.coordinate_sum += static_cast<double>(edge.second.y);
                ++totals.edges;
            }
        }
    }
    return totals;
}

void print_first_result(const rdb::Database& database, const rdb::RuleCheck& check) {
    if (check.results.empty()) return;
    const rdb::Result& result = database.results[check.results.begin];
    std::cout << "First result: ordinal=" << result.ordinal
              << ", kind="
              << (result.kind == rdb::ResultKind::Polygon ? "polygon" : "edge-cluster")
              << '\n';

    if (!result.properties.empty()) {
        const rdb::TaggedValue& property =
            database.tagged_values[result.properties.begin];
        std::cout << "First property: " << text(database, property.id)
                  << " = " << text(database, property.payload) << '\n';
    }
    if (result.kind == rdb::ResultKind::Polygon && !result.geometry.empty()) {
        const rdb::Point& point = database.vertices[result.geometry.begin];
        std::cout << "First vertex: " << point.x << ' ' << point.y << '\n';
    } else if (result.kind == rdb::ResultKind::EdgeCluster && !result.geometry.empty()) {
        const rdb::Edge& edge = database.edges[result.geometry.begin];
        std::cout << "First edge: "
                  << edge.first.x << ' ' << edge.first.y << " -> "
                  << edge.second.x << ' ' << edge.second.y << '\n';
    }
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <file.rdb> [check-name]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        rdb::IndexedRdbFile file(argv[1]);
        const rdb::Database& database = file.database();

        std::cout << "[1] Check Index -> Database\n";
        std::cout << "Top cell: " << text(database, database.top_cell_name) << '\n';
        std::cout << "Database precision: " << std::setprecision(17)
                  << database.database_precision << '\n';
        std::cout << "Indexed checks: " << database.check_count() << '\n';

        rdb::CheckId selected_id = 0U;
        if (argc == 3) {
            const std::vector<rdb::CheckId> matches = database.find_checks(argv[2]);
            if (matches.empty()) {
                std::cerr << "Check not found: " << argv[2] << '\n';
                return 3;
            }
            selected_id = matches[0];
            if (matches.size() > 1U) {
                std::cout << "Matching checks: " << matches.size()
                          << " (loading the first match)\n";
            }
        } else if (database.check_count() == 0U) {
            std::cout << "No checks found\n";
            return 0;
        }

        const rdb::RuleCheck& indexed = database.check(selected_id);
        std::cout << "Selected offset: " << indexed.offset << '\n';
        std::cout << "Indexed results: " << indexed.current_result_count << '\n';
        std::cout << "Comment: " << text(database, indexed.comment) << '\n';

        // Detail은 동일한 canonical Database의 flat pools와 RuleCheck를 append로 완성한다.
        std::cout << "[2] Detail Parser -> append to same Database\n";
        file.load_check(selected_id);
        const rdb::RuleCheck& loaded = database.check(selected_id);
        const TraversalTotals totals = traverse(database, loaded);
        const std::vector<rdb::StringId> keys = tagged_keys(database, loaded);

        std::cout << "Tagged keys:";
        for (std::size_t i = 0; i < keys.size(); ++i) {
            std::cout << ' ' << text(database, keys[i]);
        }
        std::cout << '\n';

        std::cout << "Check: " << text(database, loaded.name)
                  << " | Results: " << loaded.results.count
                  << " | Loaded checks: " << database.loaded_check_count()
                  << '/' << database.check_count() << '\n';
        std::cout << "Executed at: " << text(database, loaded.executed_at) << '\n';
        std::cout << "Geometry totals: polygons=" << totals.polygons
                  << ", edge-clusters=" << totals.edge_clusters
                  << ", vertices=" << totals.vertices
                  << ", edges=" << totals.edges
                  << ", properties=" << totals.properties << '\n';
        std::cout << "Traversed values: properties=" << totals.properties
                  << ", vertices=" << totals.vertices
                  << ", edges=" << totals.edges << '\n';
        std::cout << "Traversed payload: text-bytes=" << totals.text_bytes
                  << ", coordinate-sum=" << totals.coordinate_sum << '\n';
        print_first_result(database, loaded);
        std::cout << "Database pools: results=" << database.results.size()
                  << ", properties=" << database.tagged_values.size()
                  << ", vertices=" << database.vertices.size()
                  << ", edges=" << database.edges.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RDB error: " << error.what() << '\n';
        return 1;
    }
}
