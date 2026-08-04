#ifndef RDB_CHECK_DETAIL_HPP
#define RDB_CHECK_DETAIL_HPP

#include "rdb_check_index.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
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
    // 좌표 앞뒤 위치를 구분하지 않고 파일에서 발견한 순서대로 보관한다.
    std::vector<DetailTag> properties;
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

// 선택 Check를 TableView에 점진적으로 표시하기 위한 배치 파싱 설정이다.
// callback이 받은 vector 참조는 callback이 반환할 때까지만 유효하다.
typedef std::function<void(const std::vector<DetailResult>&)> CheckDetailBatchCallback;
typedef std::function<bool()> CheckDetailCancellationCallback;

struct CheckDetailBatchOptions {
    std::size_t batch_size;
    CheckDetailBatchCallback batch_callback;
    CheckDetailCancellationCallback is_cancelled;

    CheckDetailBatchOptions() : batch_size(10000U) {}
};

struct CheckDetailBatchResult {
    // detail에는 Check header/text만 들어 있고 results는 배치 callback으로만 전달된다.
    CheckDetail detail;
    std::size_t parsed_result_count;
    bool completed;

    CheckDetailBatchResult() : parsed_result_count(0), completed(false) {}
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
    // 좌표 앞의 태그는 properties에 보관한다.
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
        result.properties.push_back(parse_detail_tag(text));
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
        result.properties.push_back(parse_detail_tag(trim(line.text)));
    }
}

inline void consume_detail_final_tail(LineCursor& cursor, DetailResult& result) {
    // 마지막 결과 뒤에서는 다음 "규칙 이름 + 헤더" 조합을 만나면 멈춘다.
    // 이 처리가 있어야 다음 RuleCheck의 내용을 현재 결과의 태그로 잘못 넣지 않는다.
    bool have_candidate = false;
    Line candidate;
    for (;;) {
        if (!have_candidate && !next_nonblank(cursor, candidate)) return;
        have_candidate = false;

        ResultSignature extra;
        if (parse_result_signature(trim(candidate.text), extra)) {
            throw ScanError(candidate.offset, "physical result list exceeds result count in rule header");
        }

        Line possible_header;
        if (!next_nonblank(cursor, possible_header)) {
            result.properties.push_back(parse_detail_tag(trim(candidate.text)));
            return;
        }

        RuleHeader next_header;
        if (parse_rule_header(trim(possible_header.text), next_header)) {
            return;
        }

        result.properties.push_back(parse_detail_tag(trim(candidate.text)));
        // possible_header는 이미 읽었지만 다음 후보로 재사용한다.
        // mmap의 Line 포인터는 파싱 중 유효하므로 파일을 다시 읽을 필요가 없다.
        candidate = possible_header;
        have_candidate = true;
    }
}

inline void reject_undeclared_result_after_empty_check(LineCursor& cursor) {
    Line line;
    if (!next_nonblank(cursor, line)) return;
    ResultSignature signature;
    if (parse_result_signature(trim(line.text), signature)) {
        throw ScanError(line.offset, "physical result exists although current result count is zero");
    }
}

// 실제 상세 파싱 본문이다. MappedFile을 인자로 받아, 일회성 파서와 재사용 세션이 같은
// 구현을 공유하게 한다.
inline CheckDetail parse_check_detail(const MappedFile& file, CheckOffset offset) {
    LineCursor cursor(file, offset);

    Line name_line;
    if (!next_nonblank(cursor, name_line)) throw ScanError(offset, "missing rule-check name");
    Line header_line;
    if (!cursor.next(header_line)) throw ScanError(cursor.position(), "truncated rule-check header");

    RuleHeader header;
    const Span header_text = trim(header_line.text);
    if (!parse_rule_header(header_text, header)) {
        throw ScanError(header_line.offset, "expected rule-check header");
    }

    CheckDetail check;
    check.name = as_string(trim(name_line.text));
    check.offset = name_line.offset;
    check.current_result_count = header.current_result_count;
    check.original_result_count = header.original_result_count;

    // 헤더의 날짜/시간은 앞의 숫자 세 개를 소비한 나머지 전체다.
    Span words = header_text;
    Span word;
    for (int i = 0; i < 3; ++i) (void)next_word(words, word);
    check.executed_at = as_string(trim(words));

    check.check_text.reserve(static_cast<std::size_t>(header.check_text_line_count));
    check.results.reserve(header.current_result_count);
    for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated check text");
        check.check_text.push_back(as_string(line.text));
    }

    if (header.current_result_count == 0U) {
        reject_undeclared_result_after_empty_check(cursor);
        return check;
    }

    for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
        DetailResult result;
        Line signature_line;
        ResultSignature signature;
        bool found = false;
        while (next_nonblank(cursor, signature_line)) {
            // p/e 선언 전의 줄은 표준 위치의 태그로 간주한다.
            if (parse_result_signature(trim(signature_line.text), signature)) {
                found = true;
                break;
            }
            result.properties.push_back(
                parse_detail_tag(trim(signature_line.text)));
        }
        if (!found) throw ScanError(cursor.position(), "truncated result list");

        result.kind = signature.kind;
        result.ordinal = signature.ordinal;
        result.signature_suffix = as_string(signature.suffix);
        if (signature.kind == ResultKind::Polygon) {
            // vector 용량을 미리 확보하면 좌표를 읽으며 재할당하는 일을 줄일 수 있다.
            result.vertices.reserve(static_cast<std::size_t>(signature.coordinate_count));
        } else {
            result.edges.reserve(static_cast<std::size_t>(signature.coordinate_count));
        }
        consume_detail_geometry(cursor, signature, result);

        if (i + 1U == header.current_result_count) {
            consume_detail_final_tail(cursor, result);
        } else {
            consume_detail_intermediate_tail(cursor, result);
        }
        // DetailResult 안의 큰 vector들을 복사하지 않고 CheckDetail로 소유권을 옮긴다.
        check.results.push_back(std::move(result));
    }

    return check;
}

inline bool detail_parse_cancelled(const CheckDetailCancellationCallback& callback) {
    return callback && callback();
}

inline bool consume_detail_geometry_cancellable(
    LineCursor& cursor,
    const ResultSignature& signature,
    DetailResult& result,
    const CheckDetailCancellationCallback& is_cancelled) {
    std::uint64_t seen = 0;
    while (seen < signature.coordinate_count) {
        if (detail_parse_cancelled(is_cancelled)) return false;
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
        result.properties.push_back(parse_detail_tag(text));
    }
    return true;
}

inline bool consume_detail_intermediate_tail_cancellable(
    LineCursor& cursor,
    DetailResult& result,
    const CheckDetailCancellationCallback& is_cancelled) {
    for (;;) {
        if (detail_parse_cancelled(is_cancelled)) return false;
        const char* const mark = cursor.mark();
        Line line;
        if (!next_nonblank(cursor, line)) {
            throw ScanError(cursor.position(), "result count exceeds physical result list");
        }
        ResultSignature next;
        if (parse_result_signature(trim(line.text), next)) {
            cursor.reset(mark);
            return true;
        }
        result.properties.push_back(parse_detail_tag(trim(line.text)));
    }
}

inline bool consume_detail_final_tail_cancellable(
    LineCursor& cursor,
    DetailResult& result,
    const CheckDetailCancellationCallback& is_cancelled) {
    bool have_candidate = false;
    Line candidate;
    for (;;) {
        if (detail_parse_cancelled(is_cancelled)) return false;
        if (!have_candidate && !next_nonblank(cursor, candidate)) return true;
        have_candidate = false;

        ResultSignature extra;
        if (parse_result_signature(trim(candidate.text), extra)) {
            throw ScanError(candidate.offset, "physical result list exceeds result count in rule header");
        }

        Line possible_header;
        if (!next_nonblank(cursor, possible_header)) {
            result.properties.push_back(parse_detail_tag(trim(candidate.text)));
            return true;
        }

        RuleHeader next_header;
        if (parse_rule_header(trim(possible_header.text), next_header)) return true;
        result.properties.push_back(parse_detail_tag(trim(candidate.text)));
        candidate = possible_header;
        have_candidate = true;
    }
}

inline CheckDetailBatchResult parse_check_detail_batches(
    const MappedFile& file,
    CheckOffset offset,
    const CheckDetailBatchOptions& options) {
    if (options.batch_size == 0U) {
        throw std::invalid_argument("Check detail batch size must be greater than zero");
    }
    if (!options.batch_callback) {
        throw std::invalid_argument("Check detail batch callback is required");
    }

    CheckDetailBatchResult outcome;
    if (detail_parse_cancelled(options.is_cancelled)) return outcome;

    LineCursor cursor(file, offset);
    Line name_line;
    if (!next_nonblank(cursor, name_line)) throw ScanError(offset, "missing rule-check name");
    Line header_line;
    if (!cursor.next(header_line)) throw ScanError(cursor.position(), "truncated rule-check header");

    RuleHeader header;
    const Span header_text = trim(header_line.text);
    if (!parse_rule_header(header_text, header)) {
        throw ScanError(header_line.offset, "expected rule-check header");
    }

    CheckDetail& check = outcome.detail;
    check.name = as_string(trim(name_line.text));
    check.offset = name_line.offset;
    check.current_result_count = header.current_result_count;
    check.original_result_count = header.original_result_count;
    Span words = header_text;
    Span word;
    for (int i = 0; i < 3; ++i) (void)next_word(words, word);
    check.executed_at = as_string(trim(words));

    check.check_text.reserve(static_cast<std::size_t>(header.check_text_line_count));
    for (std::uint64_t i = 0; i < header.check_text_line_count; ++i) {
        if (detail_parse_cancelled(options.is_cancelled)) return outcome;
        Line line;
        if (!cursor.next(line)) throw ScanError(cursor.position(), "truncated check text");
        check.check_text.push_back(as_string(line.text));
    }

    if (header.current_result_count == 0U) {
        reject_undeclared_result_after_empty_check(cursor);
        outcome.completed = true;
        return outcome;
    }

    std::vector<DetailResult> batch;
    batch.reserve(options.batch_size);
    for (std::uint32_t i = 0; i < header.current_result_count; ++i) {
        if (detail_parse_cancelled(options.is_cancelled)) return outcome;
        DetailResult result;
        Line signature_line;
        ResultSignature signature;
        bool found = false;
        while (next_nonblank(cursor, signature_line)) {
            if (detail_parse_cancelled(options.is_cancelled)) return outcome;
            if (parse_result_signature(trim(signature_line.text), signature)) {
                found = true;
                break;
            }
            result.properties.push_back(parse_detail_tag(trim(signature_line.text)));
        }
        if (!found) throw ScanError(cursor.position(), "truncated result list");

        result.kind = signature.kind;
        result.ordinal = signature.ordinal;
        result.signature_suffix = as_string(signature.suffix);
        if (signature.kind == ResultKind::Polygon) {
            result.vertices.reserve(static_cast<std::size_t>(signature.coordinate_count));
        } else {
            result.edges.reserve(static_cast<std::size_t>(signature.coordinate_count));
        }
        if (!consume_detail_geometry_cancellable(
                cursor, signature, result, options.is_cancelled)) return outcome;

        const bool tail_complete = i + 1U == header.current_result_count
            ? consume_detail_final_tail_cancellable(cursor, result, options.is_cancelled)
            : consume_detail_intermediate_tail_cancellable(cursor, result, options.is_cancelled);
        if (!tail_complete) return outcome;

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

/*
 * UI가 같은 RDB에서 여러 Check를 선택할 때 사용하는 재사용 세션이다.
 * 생성 시 파일을 한 번만 mmap하고, parse_at()은 각 offset의 Check 본문만 읽는다.
 */
class CheckDetailFile {
public:
    explicit CheckDetailFile(const std::string& path) : file_(path) {}

    CheckDetail parse_at(CheckOffset offset) const {
        return detail::parse_check_detail(file_, offset);
    }

private:
    CheckDetailFile(const CheckDetailFile&);
    CheckDetailFile& operator=(const CheckDetailFile&);

    detail::MappedFile file_;
};

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
        return detail::parse_check_detail(file, offset);
    }

    CheckDetailBatchResult parse_file_at_batches(
        const std::string& path,
        CheckOffset offset,
        const CheckDetailBatchOptions& options) const {
        detail::MappedFile file(path);
        return detail::parse_check_detail_batches(file, offset, options);
    }
};

} // namespace rdb

#endif // RDB_CHECK_DETAIL_HPP
