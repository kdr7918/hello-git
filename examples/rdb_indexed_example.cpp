#include "rdb_compact_database.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct GeometryTotals {
    std::size_t polygons;
    std::size_t edge_clusters;
    std::size_t properties;
    std::size_t vertices;
    std::size_t edges;
    std::size_t text_bytes;
    double coordinate_sum;

    GeometryTotals()
        : polygons(0), edge_clusters(0), properties(0), vertices(0), edges(0),
          text_bytes(0), coordinate_sum(0.0) {}
};

GeometryTotals summarize(const rdb::CompactCheckView& check) {
    GeometryTotals totals;
    for (std::size_t i = 0; i < check.detail_result_count(); ++i) {
        const rdb::CompactResultView result = check.result(i);
        for (std::size_t j = 0; j < result.property_count(); ++j) {
            const rdb::CompactPropertyView property = result.property(j);
            totals.text_bytes += property.id().str().size();
            totals.text_bytes += property.payload().str().size();
            ++totals.properties;
        }
        if (result.kind() == rdb::ResultKind::Polygon) {
            ++totals.polygons;
        } else {
            ++totals.edge_clusters;
        }
        for (std::size_t j = 0; j < result.vertex_count(); ++j) {
            const rdb::Point point = result.vertex(j);
            totals.coordinate_sum += static_cast<double>(point.x);
            totals.coordinate_sum += static_cast<double>(point.y);
            ++totals.vertices;
        }
        for (std::size_t j = 0; j < result.edge_count(); ++j) {
            const rdb::Edge edge = result.edge(j);
            totals.coordinate_sum += static_cast<double>(edge.first.x);
            totals.coordinate_sum += static_cast<double>(edge.first.y);
            totals.coordinate_sum += static_cast<double>(edge.second.x);
            totals.coordinate_sum += static_cast<double>(edge.second.y);
            ++totals.edges;
        }
    }
    return totals;
}

void print_first_result(const rdb::CompactCheckView& check) {
    if (check.detail_result_count() == 0U) return;

    const rdb::CompactResultView result = check.result(0U);
    std::cout << "First result: ordinal=" << result.ordinal()
              << ", kind="
              << (result.kind() == rdb::ResultKind::Polygon ? "polygon" : "edge-cluster")
              << '\n';

    if (result.property_count() != 0U) {
        const rdb::CompactPropertyView property = result.property(0U);
        std::cout << "First property: " << property.id().str()
                  << " = " << property.payload().str() << '\n';
    }

    if (result.vertex_count() != 0U) {
        const rdb::Point point = result.vertex(0U);
        std::cout << "First vertex: " << point.x << ' ' << point.y << '\n';
    } else if (result.edge_count() != 0U) {
        const rdb::Edge edge = result.edge(0U);
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
        const rdb::CompactCheckDatabase& database = file.database();

        std::cout << "Top cell: " << database.top_cell_name().str() << '\n';
        std::cout << "Database precision: " << std::setprecision(17)
                  << database.database_precision() << '\n';
        std::cout << "Indexed checks: " << file.check_count() << '\n';

        rdb::CheckId selected_id = 0U;
        if (argc == 3) {
            const std::vector<rdb::CheckId> matches = file.find_checks(argv[2]);
            if (matches.empty()) {
                std::cerr << "Check not found: " << argv[2] << '\n';
                return 3;
            }
            selected_id = matches[0];
            if (matches.size() > 1U) {
                std::cout << "Matching checks: " << matches.size()
                          << " (loading the first match)\n";
            }
        } else if (file.check_count() == 0U) {
            std::cout << "No checks found\n";
            return 0;
        }

        const rdb::CompactCheckView indexed = file.check(selected_id);
        std::cout << "Selected offset: " << indexed.offset() << '\n';
        std::cout << "Comment: " << indexed.comment().str() << '\n';

        // load_check() parses only the selected Check and stores it in compact pools.
        const rdb::CompactCheckView loaded = file.load_check(selected_id);
        const GeometryTotals totals = summarize(loaded);

        std::cout << "Check: " << loaded.name().str()
                  << " | Results: " << loaded.detail_result_count()
                  << " | Loaded checks: " << database.loaded_check_count()
                  << '/' << database.check_count() << '\n';
        std::cout << "Executed at: " << loaded.executed_at().str() << '\n';
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

        print_first_result(loaded);

        const rdb::CompactMemoryUsage memory = database.memory_usage();
        std::cout << "Compact storage: used=" << memory.used_bytes
                  << " bytes, capacity=" << memory.capacity_bytes << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RDB error: " << error.what() << '\n';
        return 1;
    }
}
