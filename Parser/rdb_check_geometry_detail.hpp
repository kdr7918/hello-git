#ifndef RDB_CHECK_GEOMETRY_DETAIL_HPP
#define RDB_CHECK_GEOMETRY_DETAIL_HPP

#include "rdb_check_geometry.hpp"

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rdb {

/* 선택한 Check만 좌표 전용으로 읽는 배치 파서다. 태그와 check text를 복사하지
 * 않으므로 Coords Only 화면에서 CheckDetailParser보다 훨씬 적은 메모리를 쓴다. */
struct GeometryDetailResult {
    ResultKind kind;
    std::uint32_t ordinal;
    std::vector<Point> vertices;
    std::vector<Edge> edges;

    GeometryDetailResult() : kind(ResultKind::Polygon), ordinal(0) {}
};

typedef std::function<void(const std::vector<GeometryDetailResult>&)> GeometryDetailBatchCallback;
typedef std::function<bool()> GeometryDetailCancellationCallback;

struct GeometryDetailBatchOptions {
    std::size_t batch_size;
    GeometryDetailBatchCallback batch_callback;
    GeometryDetailCancellationCallback is_cancelled;

    GeometryDetailBatchOptions() : batch_size(10000U) {}
};

struct GeometryDetailBatchResult {
    std::size_t parsed_result_count;
    bool completed;

    GeometryDetailBatchResult() : parsed_result_count(0), completed(false) {}
};

namespace detail {

inline bool geometry_detail_cancelled(const GeometryDetailCancellationCallback& callback) {
    return callback && callback();
}

inline bool consume_geometry_detail(LineCursor& cursor,
                                    const ResultSignature& signature,
                                    GeometryDetailResult& result,
                                    const GeometryDetailCancellationCallback& cancellation) {
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        if (geometry_detail_cancelled(cancellation)) return false;
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated result geometry");
        const Span text = trim(line.text);
        if (text.empty()) continue;

        const char first = *text.begin;
        const bool coordinate_candidate = decimal(first) || first == '+' || first == '-';
        if (signature.kind == ResultKind::Polygon) {
            Point point;
            if (coordinate_candidate && parse_fast_point(text, point)) {
                result.vertices.push_back(point);
                ++seen;
                continue;
            }
        } else {
            Edge edge;
            if (coordinate_candidate && parse_fast_edge(text, edge)) {
                result.edges.push_back(edge);
                ++seen;
                continue;
            }
        }

        ResultSignature unexpected;
        if ((first == 'p' || first == 'e') && parse_result_signature(text, unexpected)) {
            throw ScanError(line.offset, "result ended before its declared coordinate count");
        }
        // 태그 행은 의도적으로 버린다.
    }
    return true;
}

inline GeometryDetailBatchResult parse_check_geometry_batches(
    const MappedFile& file,
    CheckOffset offset,
    const GeometryDetailBatchOptions& options) {
    if (options.batch_size == 0U) {
        throw std::invalid_argument("Geometry detail batch size must be greater than zero");
    }
    if (!options.batch_callback) {
        throw std::invalid_argument("Geometry detail batch callback is required");
    }

    GeometryDetailBatchResult outcome;
    if (geometry_detail_cancelled(options.is_cancelled)) return outcome;

    LineCursor cursor(file, offset);
    Line name_line;
    if (!next_nonblank(cursor, name_line)) throw ScanError(offset, "missing rule-check name");
    Line header_line;
    if (!cursor.next(header_line)) throw ScanError(cursor.position(), "truncated rule-check header");
    RuleHeader header;
    if (!parse_rule_header(trim(header_line.text), header)) {
        throw ScanError(header_line.offset, "expected rule-check header");
    }
    skip_check_text(cursor, header);

    std::vector<GeometryDetailResult> batch;
    batch.reserve(options.batch_size);
    for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
        if (geometry_detail_cancelled(options.is_cancelled)) return outcome;
        Line signature_line;
        ResultSignature signature;
        if (!next_result_signature(cursor, signature_line, signature)) {
            throw ScanError(cursor.position(), "truncated result list");
        }

        GeometryDetailResult result;
        result.kind = signature.kind;
        result.ordinal = signature.ordinal;
        if (signature.kind == ResultKind::Polygon) {
            result.vertices.reserve(static_cast<std::size_t>(signature.coordinate_count));
        } else {
            result.edges.reserve(static_cast<std::size_t>(signature.coordinate_count));
        }
        if (!consume_geometry_detail(cursor, signature, result, options.is_cancelled)) return outcome;
        batch.push_back(std::move(result));
        ++outcome.parsed_result_count;
        if (batch.size() == options.batch_size) {
            options.batch_callback(batch);
            batch.clear();
        }
    }
    if (!batch.empty()) options.batch_callback(batch);
    outcome.completed = true;
    return outcome;
}

} // namespace detail

class CheckGeometryDetailParser {
public:
    GeometryDetailBatchResult parse_file_at_batches(
        const std::string& path,
        CheckOffset offset,
        const GeometryDetailBatchOptions& options) const {
        detail::MappedFile file(path);
        return detail::parse_check_geometry_batches(file, offset, options);
    }
};

} // namespace rdb

#endif // RDB_CHECK_GEOMETRY_DETAIL_HPP
