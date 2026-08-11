#include "rdb_database_support.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

const std::size_t database_index_max =
    static_cast<std::size_t>(std::numeric_limits<rdb::Index>::max());

// Database의 32-bit index 범위를 넘기 전에 길이 오류를 발생시킨다.
void RequireCapacity(
    std::size_t current,
    std::size_t added,
    const char* description) {
    if (added > database_index_max || current > database_index_max - added) {
        throw std::length_error(
            std::string(description) + " exceeds rdb::Database capacity");
    }
}

} // namespace

// Parser callback의 임시 vector를 GUI thread까지 안전하게 소유하고 대기 수를 센다.
RDB_DETAIL_BATCH::RDB_DETAIL_BATCH(
    const std::vector<rdb::DetailResult>& source,
    const std::shared_ptr<std::atomic<std::size_t> >& pending)
    : values(source), pending_batches(pending) {
    if (pending_batches) pending_batches->fetch_add(1U);
}

// 배치의 마지막 shared_ptr이 해제되면 worker의 back-pressure 슬롯을 반환한다.
RDB_DETAIL_BATCH::~RDB_DETAIL_BATCH() {
    if (pending_batches) pending_batches->fetch_sub(1U);
}

// Check Index 결과만으로 Detail 영역이 비어 있는 공유 Database 골격을 만든다.
RDB_DATABASE_PTR MakeDatabaseFromIndex(const rdb::CheckIndexDatabase& index) {
    if (index.top_cell_name.empty()) {
        throw std::invalid_argument("RDB top-cell name must not be empty");
    }
    if (!std::isfinite(index.database_precision) ||
        index.database_precision <= 0.0) {
        throw std::invalid_argument(
            "RDB database precision must be finite and positive");
    }
    if (index.checks.size() >= database_index_max) {
        throw std::length_error(
            "RDB check list exceeds rdb::Database capacity");
    }

    // 먼저 전체 문자열 크기를 검증·계산해 이후 reserve가 한 번에 끝나게 한다.
    std::size_t stringBytes = index.top_cell_name.size();
    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const rdb::CheckIndexEntry& entry = index.checks[i];
        if (entry.name.empty()) {
            throw std::invalid_argument("RDB check name must not be empty");
        }
        RequireCapacity(stringBytes, entry.name.size(), "RDB string bytes");
        stringBytes += entry.name.size();
        RequireCapacity(
            stringBytes, entry.comment.size(), "RDB string bytes");
        stringBytes += entry.comment.size();
    }

    // 모든 모델이 같은 shared_ptr을 사용하므로 Check 메타데이터도 한 번만 저장된다.
    RDB_DATABASE_PTR database(new rdb::Database);
    database->database_precision = index.database_precision;
    database->strings.reserve(
        1U + index.checks.size() * 2U, stringBytes);
    database->rule_checks.reserve(index.checks.size());
    database->top_cell_name = database->strings.add(index.top_cell_name);

    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const rdb::CheckIndexEntry& source = index.checks[i];
        rdb::RuleCheck check;
        check.offset = source.offset;
        check.name = database->strings.add(source.name);
        check.comment = database->strings.add(source.comment);
        check.current_result_count = source.geometry_count;
        check.original_result_count = source.original_result_count;
        check.declared_check_text_count = source.check_text_line_count;
        database->rule_checks.push_back(check);
    }
    return database;
}

// BG와 선택 파서가 서로 쓰지 않도록 Index 메타데이터만 독립 DB로 복제한다.
RDB_DATABASE_PTR CloneIndexDatabase(const RDB_DATABASE_PTR& source) {
    if (!source) throw std::invalid_argument("RDB Database is null");
    rdb::CheckIndexDatabase index;
    index.database_precision = source->database_precision;
    index.top_cell_name = source->strings.get(source->top_cell_name).str();
    index.checks.reserve(source->rule_checks.size());
    for (std::size_t i = 0; i < source->rule_checks.size(); ++i) {
        const rdb::RuleCheck& check = source->rule_checks[i];
        rdb::CheckIndexEntry entry;
        entry.offset = check.offset;
        entry.name = source->strings.get(check.name).str();
        entry.comment = source->strings.get(check.comment).str();
        entry.geometry_count = check.current_result_count;
        entry.original_result_count = check.original_result_count;
        entry.check_text_line_count = check.declared_check_text_count;
        index.checks.push_back(entry);
    }
    return MakeDatabaseFromIndex(index);
}

// 유효하지 않은 StringId도 빈 QString으로 처리해 View 접근을 예외 없이 유지한다.
QString RDBString(const rdb::StringTable& strings, rdb::StringId id) {
    const rdb::StringRef value = strings.get(id);
    return value.data
        ? QString::fromUtf8(value.data, static_cast<int>(value.size))
        : QString();
}

// QString 변환 없이 저장 문자열과 std::string을 바이트 단위로 비교한다.
bool RDBSameText(
    const rdb::StringTable& strings,
    rdb::StringId id,
    const std::string& value) {
    const rdb::StringRef stored = strings.get(id);
    return stored.size == value.size() &&
        (stored.size == 0U ||
         std::memcmp(stored.data, value.data(), stored.size) == 0);
}
