#ifndef RDB_CHECK_GEOMETRY_HPP
#define RDB_CHECK_GEOMETRY_HPP

#include "rdb_check_index.hpp"

#include <limits>
#include <string>
#include <vector>

namespace rdb {

struct GeometryResult {
    // p/e 레코드 하나와 전역 vertices/edges 배열 안의 좌표 구간이다.
    ResultKind kind;
    std::uint32_t ordinal;
    Range geometry;

    GeometryResult() : kind(ResultKind::Polygon), ordinal(0) {}
};

struct GeometryCheck {
    // CheckIndexEntry와 비슷하지만, 이 구조체는 GeometryDatabase 내부 결과 구간도 가진다.
    std::string name;
    CheckOffset offset;
    std::uint32_t geometry_count;
    Range results;

    GeometryCheck() : offset(0), geometry_count(0) {}
};

// 이름, offset, 결과 수, 좌표만 필요한 전체 파일용 데이터다.
// check text와 태그를 저장하지 않아 Full Database보다 메모리를 덜 사용한다.
struct GeometryDatabase {
    std::vector<GeometryCheck> checks;
    std::vector<GeometryResult> results;
    std::vector<Point> vertices;
    std::vector<Edge> edges;
};

namespace detail {

inline Index checked_geometry_index(std::size_t value, CheckOffset offset, const char* description) {
    // Range는 32비트 index를 쓰므로, 벡터가 표현 범위를 넘기기 전에 명확히 실패시킨다.
    if (value > static_cast<std::size_t>(std::numeric_limits<Index>::max())) {
        throw ScanError(offset, std::string(description) + " exceeds 32-bit range capacity");
    }
    return static_cast<Index>(value);
}

/*
 * 좌표 전용 hot path다. next_word()/Span trim()을 여러 번 호출하지 않고 한 줄의
 * 정수를 포인터로 직접 변환한다. 수백만 좌표를 읽을 때 이 경로의 비용 차이가 크다.
 */
inline bool parse_fast_signed_coordinate(const char*& cursor,
                                         const char* end,
                                         std::int64_t& result) {
    while (cursor != end && space(*cursor)) ++cursor;
    if (cursor == end) return false;

    bool negative = false;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        ++cursor;
    }

    const char* const first_digit = cursor;
    std::uint64_t magnitude = 0;
    const std::uint64_t positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = negative ? positive_limit + 1U : positive_limit;
    while (cursor != end && decimal(*cursor)) {
        const std::uint64_t digit = static_cast<std::uint64_t>(*cursor - '0');
        if (magnitude > (limit - digit) / 10U) return false;
        magnitude = magnitude * 10U + digit;
        ++cursor;
    }
    if (cursor == first_digit) return false;

    if (negative && magnitude == positive_limit + 1U) {
        result = std::numeric_limits<std::int64_t>::min();
    } else {
        result = static_cast<std::int64_t>(magnitude);
        if (negative) result = -result;
    }
    return true;
}

inline bool only_geometry_space(const char* cursor, const char* end) {
    while (cursor != end && space(*cursor)) ++cursor;
    return cursor == end;
}

inline bool parse_fast_point(Span text, Point& point) {
    const char* cursor = text.begin;
    return parse_fast_signed_coordinate(cursor, text.end, point.x) &&
           parse_fast_signed_coordinate(cursor, text.end, point.y) &&
           only_geometry_space(cursor, text.end);
}

inline bool parse_fast_edge(Span text, Edge& edge) {
    const char* cursor = text.begin;
    return parse_fast_signed_coordinate(cursor, text.end, edge.first.x) &&
           parse_fast_signed_coordinate(cursor, text.end, edge.first.y) &&
           parse_fast_signed_coordinate(cursor, text.end, edge.second.x) &&
           parse_fast_signed_coordinate(cursor, text.end, edge.second.y) &&
           only_geometry_space(cursor, text.end);
}

template <typename Value>
inline void reserve_geometry_values(std::vector<Value>& values,
                                    std::uint64_t additional,
                                    CheckOffset offset,
                                    const char* description) {
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<Index>::max());
    if (additional > maximum || values.size() > maximum - static_cast<std::size_t>(additional)) {
        throw ScanError(offset, std::string(description) + " exceeds 32-bit range capacity");
    }

    const std::size_t required = values.size() + static_cast<std::size_t>(additional);
    if (required <= values.capacity()) return;

    // 선언된 좌표 수를 이용해 큰 result 하나를 읽는 동안의 반복 재할당을 없앤다.
    std::size_t new_capacity = values.capacity() < 1024U ? 1024U : values.capacity();
    while (new_capacity < required && new_capacity <= maximum / 2U) new_capacity *= 2U;
    if (new_capacity < required) new_capacity = required;
    values.reserve(new_capacity);
}

inline void consume_geometry(LineCursor& cursor,
                             const ResultSignature& signature,
                             GeometryDatabase& database) {
    // 상세 파서와 달리 태그 문자열은 만들지 않고 좌표만 전역 배열에 추가한다.
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated result geometry");
        const Span text = line.text;
        const Span trimmed = trim(text);
        if (trimmed.empty()) continue;

        // 태그는 대부분 문자로 시작한다. 숫자/부호로 시작하는 행만 비싼 정수 변환을 시도한다.
        const char first = *trimmed.begin;
        const bool coordinate_candidate = decimal(first) || first == '+' || first == '-';

        if (signature.kind == ResultKind::Polygon) {
            Point point;
            if (coordinate_candidate && parse_fast_point(text, point)) {
                database.vertices.push_back(point);
                ++seen;
                continue;
            }
        } else {
            Edge edge;
            if (coordinate_candidate && parse_fast_edge(text, edge)) {
                database.edges.push_back(edge);
                ++seen;
                continue;
            }
        }

        ResultSignature unexpected;
        if ((first == 'p' || first == 'e') && parse_result_signature(trimmed, unexpected)) {
            throw ScanError(line.offset, "result ended before its declared coordinate count");
        }
        // 좌표가 아닌 행은 태그다. 이 모드에서는 의도적으로 버린다.
    }
}

} // namespace detail

/*
 * 3단계용 전체 좌표 파서다.
 *
 * 파일을 한 번 끝까지 읽되 RuleCheck text와 태그는 건너뛴다. 이름, offset,
 * p/e 메타데이터, 좌표만 남기므로 좌표 테이블/미니맵용 백그라운드 로딩에 맞는다.
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
        // 대부분의 RDB는 적어도 몇 개의 Check를 가지므로, 작은 초기 재할당을 피한다.
        database.checks.reserve(1024);
        detail::Line name_line;
        detail::RuleHeader header;
        while (detail::next_rule(cursor, name_line, header)) {
            GeometryCheck check;
            const detail::Span name = detail::trim(name_line.text);
            check.name.assign(name.begin, name.size());
            check.offset = name_line.offset;
            check.geometry_count = header.current_result_count;

            detail::skip_check_text(cursor, header);
            // Check가 차지할 GeometryResult 시작 위치를 기억해 Range로 연결한다.
            detail::reserve_geometry_values(
                database.results, header.current_result_count, check.offset, "result count");
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

                if (signature.kind == ResultKind::Polygon) {
                    detail::reserve_geometry_values(
                        database.vertices, signature.coordinate_count, signature_line.offset, "vertex count");
                } else {
                    detail::reserve_geometry_values(
                        database.edges, signature.coordinate_count, signature_line.offset, "edge count");
                }

                detail::consume_geometry(cursor, signature, database);
                // p면 vertices, e면 edges 배열에서 이번 결과가 추가한 구간만 기록한다.
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
