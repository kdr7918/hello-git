#include "ascii_rdb_parser.hpp"
#include "rdb_check_detail.hpp"
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

    rdb::ParseOptions full_cancel_options;
    full_cancel_options.is_cancelled = []() { return true; };
    bool full_parse_cancelled = false;
    try {
        parser.parse_file(sample_path("large_standard_sample.rdb"), full_cancel_options);
    } catch (const rdb::ParseCancelled&) {
        full_parse_cancelled = true;
    }
    RDB_CHECK(full_parse_cancelled);

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

    rdb::FastCheckIndexOptions index_cancel_options;
    index_cancel_options.is_cancelled = []() { return true; };
    bool index_parse_cancelled = false;
    try {
        index_parser.parse_database(sample_path("large_standard_sample.rdb"), index_cancel_options);
    } catch (const rdb::ScanCancelled&) {
        index_parse_cancelled = true;
    }
    RDB_CHECK(index_parse_cancelled);

    // 1차 scan과 comment pread 사이의 동일-size 파일 변경도 감지해야 한다.
    const TemporaryRdb file_state_fixture("TOP 0.001\n");
    const int state_fd = ::open(file_state_fixture.path().c_str(), O_RDWR | O_CLOEXEC);
    RDB_CHECK(state_fd >= 0);
    const rdb::detail::FileState state_before = rdb::detail::capture_file_state(state_fd);
    RDB_CHECK(::pwrite(state_fd, "X", 1, 0) == 1);
    RDB_CHECK(::fsync(state_fd) == 0);
    const rdb::detail::FileState state_after = rdb::detail::capture_file_state(state_fd);
    RDB_CHECK(!rdb::detail::same_file_state(state_before, state_after));
    RDB_CHECK(::close(state_fd) == 0);

    const rdb::CheckIndexDatabase index_database =
        index_parser.parse_database(sample_path("standard_sample.rdb"));
    static_assert(
        std::is_same<decltype(index_database.database_precision), double>::value,
        "fast index database precision must be double");
    static_assert(
        std::is_same<decltype(index_database.checks[0].comment), std::string>::value,
        "fast index comment must be one std::string");
    RDB_CHECK(index_database.top_cell_name == "TOP_CHIP");
    RDB_CHECK(index_database.database_precision == 1000.0);
    RDB_CHECK(index_database.checks.size() == 3);
    RDB_CHECK(index_database.checks[0].name == "M1.SPACING.1");
    RDB_CHECK(index_database.checks[0].comment ==
        "Rule File Pathname: ./demo.svrf\n"
        "Rule File Title: Example DRC deck\n"
        "M1 spacing must be at least 0.14 um.");
    RDB_CHECK(index_database.checks[1].comment ==
        "Rule File Pathname: ./demo.svrf\n"
        "M2 density is below the required threshold.");
    RDB_CHECK(index_database.checks[2].comment ==
        "Example of a rule check with no remaining defects.");

    const std::vector<rdb::CheckIndexEntry> index =
        index_parser.parse_file(sample_path("standard_sample.rdb"));
    RDB_CHECK(index.size() == 3);
    RDB_CHECK(index[0].name == "M1.SPACING.1");
    RDB_CHECK(index[0].offset > 0);
    RDB_CHECK(index[0].geometry_count == 2);
    RDB_CHECK(index[0].comment == index_database.checks[0].comment);
    RDB_CHECK(index[1].geometry_count == 1);
    RDB_CHECK(index[2].geometry_count == 0);

    // Fast parser는 전체 scan/comment 보강 진행률을 0~100 범위로 단조 증가시킨다.
    rdb::FastCheckIndexOptions progress_options;
    progress_options.read_buffer_bytes = 71;
    progress_options.context_bytes = 64;
    std::vector<int> progress_values;
    progress_options.progress_callback = [&progress_values](int value) {
        progress_values.push_back(value);
    };
    const rdb::CheckIndexDatabase progress_index = index_parser.parse_database(
        sample_path("large_standard_sample.rdb"), progress_options);
    RDB_CHECK(progress_index.checks.size() == 100);
    RDB_CHECK(progress_values.size() > 3);
    RDB_CHECK(progress_values.front() == 0);
    RDB_CHECK(progress_values.back() == 100);
    for (std::size_t i = 0; i < progress_values.size(); ++i) {
        RDB_CHECK(progress_values[i] >= 0);
        RDB_CHECK(progress_values[i] <= 100);
        if (i != 0) RDB_CHECK(progress_values[i - 1] < progress_values[i]);
    }

    // 90 callback에서 동일-size 변경이 생기면 100을 보내지 않고 거부한다.
    const TemporaryRdb changing_file(
        "TOP 0.001\nCHANGING.CHECK\n0 0 1 Jul 21 12:10:49 2026\ncomment\n");
    const int changing_fd = ::open(changing_file.path().c_str(), O_RDWR | O_CLOEXEC);
    RDB_CHECK(changing_fd >= 0);
    std::vector<int> changing_progress;
    rdb::FastCheckIndexOptions changing_options;
    changing_options.progress_callback = [&changing_progress, changing_fd](int value) {
        changing_progress.push_back(value);
        if (value == 90) {
            if (::pwrite(changing_fd, "X", 1, 0) != 1 || ::fsync(changing_fd) != 0) {
                throw std::runtime_error("cannot mutate progress test file");
            }
        }
    };
    bool changing_file_rejected = false;
    try {
        index_parser.parse_database(changing_file.path(), changing_options);
    } catch (const rdb::ScanError&) {
        changing_file_rejected = true;
    }
    RDB_CHECK(changing_file_rejected);
    RDB_CHECK(!changing_progress.empty());
    RDB_CHECK(changing_progress.back() != 100);
    RDB_CHECK(::close(changing_fd) == 0);

    // Callback 예외는 삼키지 않고 호출자에게 그대로 전달하며 100을 보내지 않는다.
    struct ProgressAbort {};
    std::vector<int> abort_progress;
    rdb::FastCheckIndexOptions abort_options;
    abort_options.progress_callback = [&abort_progress](int value) {
        abort_progress.push_back(value);
        if (value >= 90) throw ProgressAbort();
    };
    bool callback_exception_propagated = false;
    try {
        index_parser.parse_database(sample_path("standard_sample.rdb"), abort_options);
    } catch (const ProgressAbort&) {
        callback_exception_propagated = true;
    }
    RDB_CHECK(callback_exception_propagated);
    RDB_CHECK(!abort_progress.empty());
    RDB_CHECK(abort_progress.back() != 100);

    // 0 callback에서 파일이 크게 늘어나도 진행률 계산은 int 범위를 넘지 않는다.
    const TemporaryRdb growing_file("X");
    const int growing_fd = ::open(growing_file.path().c_str(), O_RDWR | O_CLOEXEC);
    RDB_CHECK(growing_fd >= 0);
    std::vector<int> growing_progress;
    rdb::FastCheckIndexOptions growing_options;
    growing_options.progress_callback = [&growing_progress, growing_fd](int value) {
        growing_progress.push_back(value);
        if (value == 0 &&
            (::pwrite(growing_fd, "T 1\n", 4, 0) != 4 ||
             ::ftruncate(growing_fd, 24 * 1024 * 1024) != 0)) {
            throw std::runtime_error("cannot grow progress test file");
        }
    };
    bool growing_file_rejected = false;
    try {
        index_parser.parse_database(growing_file.path(), growing_options);
    } catch (const rdb::ScanError&) {
        growing_file_rejected = true;
    }
    RDB_CHECK(growing_file_rejected);
    RDB_CHECK(!growing_progress.empty());
    RDB_CHECK(growing_progress.back() != 100);
    for (std::size_t i = 0; i < growing_progress.size(); ++i) {
        RDB_CHECK(growing_progress[i] >= 0);
        RDB_CHECK(growing_progress[i] <= 100);
    }
    RDB_CHECK(::close(growing_fd) == 0);

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
    RDB_CHECK(blank_header_index.checks[0].comment.empty());

    // 시간 header 뒤부터 첫 p/e signature 전까지 선언된 comment 줄을 원문대로 보존한다.
    const TemporaryRdb comment_file(
        "TOP 0.001\r\nCOMMENT.CHECK\r\n1 1 3 Jul 21 12:10:49 2026\r\n"
        "first comment\r\n\r\n  third comment  \r\np 1 1\r\n0 0\r\n");
    const rdb::CheckIndexDatabase comment_index =
        index_parser.parse_database(comment_file.path());
    RDB_CHECK(comment_index.checks.size() == 1);
    RDB_CHECK(comment_index.checks[0].comment ==
        "first comment\n\n  third comment  ");

    // Comment 안의 가짜 이름/header 시간 패턴은 별도 Check로 인덱싱하지 않는다.
    const TemporaryRdb header_like_comment_file(
        "TOP 0.001\nREAL.CHECK\n1 1 3 Jul 21 12:10:49 2026\n"
        "NOT.A.CHECK\n7 7 0 Jul 21 12:10:50 2026\nlast comment\n"
        "p 1 1\n0 0\n");
    const rdb::CheckIndexDatabase header_like_comment_index =
        index_parser.parse_database(header_like_comment_file.path());
    RDB_CHECK(header_like_comment_index.checks.size() == 1);
    RDB_CHECK(header_like_comment_index.checks[0].name == "REAL.CHECK");
    RDB_CHECK(header_like_comment_index.checks[0].comment ==
        "NOT.A.CHECK\n7 7 0 Jul 21 12:10:50 2026\nlast comment");

    // Comment가 pread block 경계를 넘고 마지막 LF가 없어도 전체 문자열을 보존한다.
    const std::string long_comment(10000, 'C');
    const TemporaryRdb long_comment_file(
        std::string("TOP 0.001\nLONG.COMMENT\n0 0 1 Jul 21 12:10:49 2026\n") +
        long_comment);
    const rdb::CheckIndexDatabase long_comment_index =
        index_parser.parse_database(long_comment_file.path());
    RDB_CHECK(long_comment_index.checks.size() == 1);
    RDB_CHECK(long_comment_index.checks[0].comment == long_comment);

    // 첫/마지막/all-blank comment 행도 하나의 문자열 안에서 newline으로 보존한다.
    const TemporaryRdb boundary_blank_comment_file(
        "TOP 0.001\nBOUNDARY.BLANK\n0 0 3 Jul 21 12:10:49 2026\n"
        "\nmiddle\n\nALL.BLANK\n0 0 2 Jul 21 12:10:50 2026\n\n\n");
    const rdb::CheckIndexDatabase boundary_blank_comment_index =
        index_parser.parse_database(boundary_blank_comment_file.path());
    RDB_CHECK(boundary_blank_comment_index.checks.size() == 2);
    RDB_CHECK(boundary_blank_comment_index.checks[0].comment == "\nmiddle\n");
    RDB_CHECK(boundary_blank_comment_index.checks[1].comment == "\n");

    // Header가 선언한 comment 줄 수보다 파일이 짧으면 조용히 누락시키지 않는다.
    const TemporaryRdb truncated_comment_file(
        "TOP 0.001\nTRUNCATED.COMMENT\n1 1 2 Jul 21 12:10:49 2026\nonly one\n");
    bool truncated_comment_rejected = false;
    try {
        index_parser.parse_database(truncated_comment_file.path());
    } catch (const rdb::ScanError&) {
        truncated_comment_rejected = true;
    }
    RDB_CHECK(truncated_comment_rejected);

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
    RDB_CHECK(first_check.results[0].properties.size() == 5);
    RDB_CHECK(first_check.results[1].edges.size() == 2);

    const rdb::CheckDetailFile detail_file(sample_path("standard_sample.rdb"));
    const rdb::CheckDetail second_check = detail_file.parse_at(index[1].offset);
    RDB_CHECK(second_check.name == "M2.DENSITY.MIN");
    RDB_CHECK(second_check.results.size() == 1);
    RDB_CHECK(second_check.results[0].vertices.size() == 4);

    const rdb::CheckDetail extended_check = detail_parser.parse_file_at(
        sample_path("post_coordinate_tags_sample.rdb"),
        index_parser.parse_file(sample_path("post_coordinate_tags_sample.rdb"))[0].offset);
    RDB_CHECK(extended_check.results[0].properties.size() == 2);
    RDB_CHECK(extended_check.results[0].properties[0].id == "PP");
    RDB_CHECK(extended_check.results[0].properties[1].id == "PA");
    RDB_CHECK(extended_check.results[1].properties.size() == 3);
    RDB_CHECK(extended_check.results[1].properties[0].id == "EL");
    RDB_CHECK(extended_check.results[1].properties[2].id == "CN");

    // 좌표 앞뒤에 있던 property는 파일에서 발견한 순서대로 하나의 vector에 보관한다.
    const TemporaryRdb mixed_property_file(
        "TOP 1000\nMIXED.PROPERTIES\n1 1 0 Jul 21 10:35:00 2026\n"
        "PB before geometry\np 1 1\n0 0\nPA after geometry\n");
    const rdb::CheckOffset mixed_property_offset =
        index_parser.parse_file(mixed_property_file.path())[0].offset;
    const rdb::CheckDetail mixed_property_check =
        detail_parser.parse_file_at(mixed_property_file.path(), mixed_property_offset);
    RDB_CHECK(mixed_property_check.results[0].properties.size() == 2);
    RDB_CHECK(mixed_property_check.results[0].properties[0].id == "PB");
    RDB_CHECK(mixed_property_check.results[0].properties[0].payload == "before geometry");
    RDB_CHECK(mixed_property_check.results[0].properties[1].id == "PA");
    RDB_CHECK(mixed_property_check.results[0].properties[1].payload == "after geometry");

    std::size_t mixed_property_batches = 0;
    rdb::CheckDetailBatchOptions mixed_property_options;
    mixed_property_options.batch_size = 1U;
    mixed_property_options.batch_callback = [&mixed_property_batches](
        const std::vector<rdb::DetailResult>& batch) {
        ++mixed_property_batches;
        RDB_CHECK(batch.size() == 1U);
        RDB_CHECK(batch[0].properties.size() == 2U);
        RDB_CHECK(batch[0].properties[0].id == "PB");
        RDB_CHECK(batch[0].properties[1].id == "PA");
    };
    const rdb::CheckDetailBatchResult mixed_property_batch_result =
        detail_parser.parse_file_at_batches(
            mixed_property_file.path(), mixed_property_offset, mixed_property_options);
    RDB_CHECK(mixed_property_batch_result.completed);
    RDB_CHECK(mixed_property_batches == 1U);

    // 배치 파서는 정확히 batch_size개마다 결과를 전달하고 마지막 잔여분도 보낸다.
    std::string batched_contents = "TOP 1000\nBATCH.CHECK\n20001 20001 0 Jul 27 12:10:49 2026\n";
    for (std::size_t i = 0; i < 20001U; ++i) {
        batched_contents += "p " + std::to_string(i + 1U) + " 1\n";
        batched_contents += std::to_string(i) + " " + std::to_string(i + 1U) + "\n";
    }
    const TemporaryRdb batched_file(batched_contents);
    const rdb::CheckOffset batched_offset =
        index_parser.parse_file(batched_file.path())[0].offset;
    std::vector<std::size_t> batch_sizes;
    rdb::CheckDetailBatchOptions batch_options;
    batch_options.batch_size = 10000U;
    batch_options.batch_callback = [&batch_sizes](const std::vector<rdb::DetailResult>& batch) {
        batch_sizes.push_back(batch.size());
    };
    const rdb::CheckDetailBatchResult batched =
        detail_parser.parse_file_at_batches(batched_file.path(), batched_offset, batch_options);
    RDB_CHECK(batched.completed);
    RDB_CHECK(batched.parsed_result_count == 20001U);
    RDB_CHECK(batched.detail.name == "BATCH.CHECK");
    RDB_CHECK(batched.detail.results.empty());
    RDB_CHECK(batch_sizes.size() == 3U);
    RDB_CHECK(batch_sizes[0] == 10000U);
    RDB_CHECK(batch_sizes[1] == 10000U);
    RDB_CHECK(batch_sizes[2] == 1U);

    // 선택 변경 시 cancellation callback이 true가 되면 기존 파싱은 다음 결과 전에 중단한다.
    bool cancel_requested = false;
    std::size_t delivered_before_cancel = 0;
    rdb::CheckDetailBatchOptions cancel_options;
    cancel_options.batch_size = 100U;
    cancel_options.is_cancelled = [&cancel_requested]() { return cancel_requested; };
    cancel_options.batch_callback = [&cancel_requested, &delivered_before_cancel](
        const std::vector<rdb::DetailResult>& batch) {
        delivered_before_cancel += batch.size();
        cancel_requested = true;
    };
    const rdb::CheckDetailBatchResult cancelled =
        detail_parser.parse_file_at_batches(batched_file.path(), batched_offset, cancel_options);
    RDB_CHECK(!cancelled.completed);
    RDB_CHECK(cancelled.parsed_result_count == 100U);
    RDB_CHECK(delivered_before_cancel == 100U);

    std::cout << "rdb-parser-tests: OK\n";
}
