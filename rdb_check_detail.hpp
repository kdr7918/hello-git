#ifndef RDB_CHECK_DETAIL_HPP
#define RDB_CHECK_DETAIL_HPP

#include "rdb_check_index.hpp"

#include <string>
#include <vector>

namespace rdb {

struct DetailTag {
    // 예: "PP M1 spacing marker"는 id="PP", payload="M1 spacing marker"가 된다.
    std::string id;
    std::string payload;
};

struct DetailResult {
    // p/e 하나의 상세 데이터다. Polygon이면 vertices, EdgeCluster이면 edges를 사용한다.
    ResultKind kind;
    std::uint32_t ordinal;
    std::string signature_suffix;
    std::vector<DetailTag> properties_before_geometry;
    std::vector<DetailTag> properties_after_geometry;
    std::vector<Point> vertices;
    std::vector<Edge> edges;

    DetailResult() : kind(ResultKind::Polygon), ordinal(0) {}
};

// 선택한 RuleCheck 하나를 표시하는 데 필요한 완전한 데이터다.
// 전체 RDB가 아니라 선택된 Check만 메모리에 담는 것이 목적이다.
struct CheckDetail {
    std::string name;
    CheckOffset offset;
    std::string executed_at;
    std::uint32_t current_result_count;
    std::uint32_t original_result_count;
    std::vector<std::string> check_text;
    std::vector<DetailResult> results;

    CheckDetail()
        : offset(0), current_result_count(0), original_result_count(0) {}
};

namespace detail {

inline std::string as_string(Span value) {
    // mmap 파일을 닫은 뒤에도 문자열을 사용할 수 있도록 필요한 경우에만 복사한다.
    return std::string(value.begin, value.size());
}

inline DetailTag parse_detail_tag(Span text) {
    // 태그 형식은 "ID 나머지 내용"이다. payload에는 공백이 포함될 수 있다.
    Span cursor = trim(text);
    Span id;
    if (!next_word(cursor, id)) throw std::logic_error("empty tag passed to parse_detail_tag");
    DetailTag tag;
    tag.id.assign(id.begin, id.size());
    const Span payload = trim(cursor);
    tag.payload.assign(payload.begin, payload.size());
    return tag;
}

inline void consume_detail_geometry(LineCursor& cursor,
                                    const ResultSignature& signature,
                                    DetailResult& result) {
    // 선언된 좌표 수를 모두 읽을 때까지 진행한다.
    // 좌표 앞의 태그는 properties_before_geometry에 보관한다.
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated result geometry");
        const Span text = trim(line.text);
        if (text.empty()) continue;

        if (signature.kind == ResultKind::Polygon) {
            Point point;
            if (parse_point(text, point)) {
                result.vertices.push_back(point);
                ++seen;
                continue;
            }
        } else {
            Edge edge;
            if (parse_edge(text, edge)) {
                result.edges.push_back(edge);
                ++seen;
                continue;
            }
        }

        ResultSignature unexpected;
        if (parse_result_signature(text, unexpected)) {
            throw ScanError(line.offset, "result ended before its declared coordinate count");
        }
        result.properties_before_geometry.push_back(parse_detail_tag(text));
    }
}

inline void consume_detail_intermediate_tail(LineCursor& cursor, DetailResult& result) {
    // 현재 결과 뒤의 태그를 모으되, 다음 p/e 선언을 미리 읽으면 위치를 되돌린다.
    for (;;) {
        const char* const mark = cursor.mark();
        Line line;
        if (!next_nonblank(cursor, line)) {
            throw ScanError(cursor.position(), "result count exceeds physical result list");
        }
        ResultSignature next;
        if (parse_result_signature(trim(line.text), next)) {
            cursor.reset(mark);
            return;
        }
        result.properties_after_geometry.push_back(parse_detail_tag(trim(line.text)));
    }
}

inline void consume_detail_final_tail(LineCursor& cursor, DetailResult& result) {
    // 마지막 결과 뒤에서는 다음 "규칙 이름 + 헤더" 조합을 만나면 멈춘다.
    // 이 처리가 있어야 다음 RuleCheck의 내용을 현재 결과의 태그로 잘못 넣지 않는다.
    for (;;) {
        Line candidate;
        if (!next_nonblank(cursor, candidate)) return;

        ResultSignature extra;
        if (parse_result_signature(trim(candidate.text), extra)) {
            throw ScanError(candidate.offset, "physical result list exceeds result count in rule header");
        }

        const char* const after_candidate = cursor.mark();
        Line possible_header;
        if (!next_nonblank(cursor, possible_header)) {
            result.properties_after_geometry.push_back(parse_detail_tag(trim(candidate.text)));
            return;
        }

        RuleHeader next_header;
        if (parse_rule_header(trim(possible_header.text), next_header)) {
            return;
        }

        result.properties_after_geometry.push_back(parse_detail_tag(trim(candidate.text)));
        cursor.reset(after_candidate);
    }
}

} // namespace detail

/*
 * 2단계용 선택 Check 상세 파서다.
 *
 * FastCheckIndexParser가 돌려준 offset에서 시작한다. 앞선 Check는 다시 읽거나
 * 메모리에 적재하지 않으므로 사용자가 TreeView 항목을 선택했을 때 적합하다.
 */
class CheckDetailParser {
public:
    CheckDetail parse_file_at(const std::string& path, CheckOffset offset) const {
        detail::MappedFile file(path);
        detail::LineCursor cursor(file, offset);

        detail::Line name_line;
        if (!detail::next_nonblank(cursor, name_line)) throw ScanError(offset, "missing rule-check name");
        detail::Line header_line;
        if (!cursor.next(header_line)) throw ScanError(cursor.position(), "truncated rule-check header");

        detail::RuleHeader header;
        const detail::Span header_text = detail::trim(header_line.text);
        if (!detail::parse_rule_header(header_text, header)) {
            throw ScanError(header_line.offset, "expected rule-check header");
        }

        CheckDetail check;
        // 헤더의 날짜/시간은 앞의 숫자 세 개를 소비한 나머지 전체다.
        check.name = detail::as_string(detail::trim(name_line.text));
        check.offset = name_line.offset;
        check.current_result_count = header.current_result_count;
        check.original_result_count = header.original_result_count;

        detail::Span words = header_text;
        detail::Span word;
        for (int i = 0; i < 3; ++i) (void)detail::next_word(words, word);
        check.executed_at = detail::as_string(detail::trim(words));

        check.check_text.reserve(static_cast<std::size_t>(header.check_text_line_count));
        check.results.reserve(header.current_result_count);
        for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
            detail::Line line;
            if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated check text");
            check.check_text.push_back(detail::as_string(line.text));
        }

        for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
            DetailResult result;
            detail::Line signature_line;
            detail::ResultSignature signature;
            bool found = false;
            while (detail::next_nonblank(cursor, signature_line)) {
                // p/e 선언 전의 줄은 표준 위치의 태그로 간주한다.
                if (detail::parse_result_signature(detail::trim(signature_line.text), signature)) {
                    found = true;
                    break;
                }
                result.properties_before_geometry.push_back(
                    detail::parse_detail_tag(detail::trim(signature_line.text)));
            }
            if (!found) throw ScanError(cursor.position(), "truncated result list");

            result.kind = signature.kind;
            result.ordinal = signature.ordinal;
            result.signature_suffix = detail::as_string(signature.suffix);
            if (signature.kind == ResultKind::Polygon) {
                // vector 용량을 미리 확보하면 좌표를 읽으며 재할당하는 일을 줄일 수 있다.
                result.vertices.reserve(static_cast<std::size_t>(signature.coordinate_count));
            } else {
                result.edges.reserve(static_cast<std::size_t>(signature.coordinate_count));
            }
            detail::consume_detail_geometry(cursor, signature, result);

            if (i + 1U == header.current_result_count) {
                detail::consume_detail_final_tail(cursor, result);
            } else {
                detail::consume_detail_intermediate_tail(cursor, result);
            }
            check.results.push_back(result);
        }

        return check;
    }
};

} // namespace rdb

#endif // RDB_CHECK_DETAIL_HPP
