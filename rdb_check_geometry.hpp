#ifndef RDB_CHECK_GEOMETRY_HPP
#define RDB_CHECK_GEOMETRY_HPP

#include "rdb_check_index.hpp"

#include <limits>
#include <string>
#include <vector>

namespace rdb {

struct GeometryResult {
    ResultKind kind;
    std::uint32_t ordinal;
    Range geometry;

    GeometryResult() : kind(ResultKind::Polygon), ordinal(0) {}
};

struct GeometryCheck {
    std::string name;
    CheckOffset offset;
    std::uint32_t geometry_count;
    Range results;

    GeometryCheck() : offset(0), geometry_count(0) {}
};

/* File-wide data for workflows that need names, offsets, result counts, and coordinates only. */
struct GeometryDatabase {
    std::vector<GeometryCheck> checks;
    std::vector<GeometryResult> results;
    std::vector<Point> vertices;
    std::vector<Edge> edges;
};

namespace detail {

inline Index checked_geometry_index(std::size_t value, CheckOffset offset, const char* description) {
    if (value > static_cast<std::size_t>(std::numeric_limits<Index>::max())) {
        throw ScanError(offset, std::string(description) + " exceeds 32-bit range capacity");
    }
    return static_cast<Index>(value);
}

inline void consume_geometry(LineCursor& cursor,
                             const ResultSignature& signature,
                             GeometryDatabase& database) {
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated result geometry");
        const Span text = trim(line.text);
        if (text.empty()) continue;

        if (signature.kind == ResultKind::Polygon) {
            Point point;
            if (parse_point(text, point)) {
                database.vertices.push_back(point);
                ++seen;
                continue;
            }
        } else {
            Edge edge;
            if (parse_edge(text, edge)) {
                database.edges.push_back(edge);
                ++seen;
                continue;
            }
        }

        ResultSignature unexpected;
        if (parse_result_signature(text, unexpected)) {
            throw ScanError(line.offset, "result ended before its declared coordinate count");
        }
        /* Non-coordinate lines are tagged values and deliberately ignored. */
    }
}

} // namespace detail

/*
 * One-pass coordinate-only loader.  Rule text and tagged values are skipped;
 * only rule identity, file offset, result count, p/e metadata, and geometry
 * are retained.
 */
class CheckGeometryParser {
public:
    GeometryDatabase parse_file(const std::string& path) const {
        detail::MappedFile file(path);
        detail::LineCursor cursor(file);
        detail::Line top_header;
        if (!detail::next_nonblank(cursor, top_header)) throw ScanError(0, "empty RDB file");
        detail::validate_database_header(top_header);

        GeometryDatabase database;
        detail::Line name_line;
        detail::RuleHeader header;
        while (detail::next_rule(cursor, name_line, header)) {
            GeometryCheck check;
            const detail::Span name = detail::trim(name_line.text);
            check.name.assign(name.begin, name.size());
            check.offset = name_line.offset;
            check.geometry_count = header.current_result_count;

            detail::skip_check_text(cursor, header);
            const Index result_begin = detail::checked_geometry_index(
                database.results.size(), check.offset, "result begin");

            for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
                detail::Line signature_line;
                detail::ResultSignature signature;
                if (!detail::next_result_signature(cursor, signature_line, signature)) {
                    throw ScanError(cursor.position(), "truncated result list");
                }

                GeometryResult result;
                result.kind = signature.kind;
                result.ordinal = signature.ordinal;
                const Index geometry_begin = signature.kind == ResultKind::Polygon
                    ? detail::checked_geometry_index(database.vertices.size(), signature_line.offset, "vertex begin")
                    : detail::checked_geometry_index(database.edges.size(), signature_line.offset, "edge begin");

                detail::consume_geometry(cursor, signature, database);
                const std::size_t geometry_size = signature.kind == ResultKind::Polygon
                    ? database.vertices.size() - geometry_begin
                    : database.edges.size() - geometry_begin;
                result.geometry = Range(geometry_begin, detail::checked_geometry_index(
                    geometry_size, signature_line.offset, "geometry count"));
                database.results.push_back(result);
            }

            check.results = Range(result_begin, detail::checked_geometry_index(
                database.results.size() - result_begin, check.offset, "result count"));
            database.checks.push_back(check);
        }
        return database;
    }
};

} // namespace rdb

#endif // RDB_CHECK_GEOMETRY_HPP
