#include "rdb_model.hpp"

#include <QVariant>

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {

const std::size_t database_index_max =
    static_cast<std::size_t>(std::numeric_limits<rdb::Index>::max());

// 공용 배열 추가 크기가 Database index 범위를 넘지 않는지 검증한다.
void RequireCapacity(
    std::size_t current,
    std::size_t added,
    const char* description) {
    if (added > database_index_max || current > database_index_max - added) {
        throw std::length_error(
            std::string(description) + " exceeds rdb::Database capacity");
    }
}

// 숫자로 해석 가능한 property payload는 숫자 QVariant로 반환한다.
QVariant PayloadVariant(const QString& text) {
    bool numeric = false;
    const double value = text.toDouble(&numeric);
    return numeric ? QVariant(value) : QVariant(text);
}

// 좌표 한 점을 기존 공백 구분 문자열 형식으로 덧붙인다.
void AppendPoint(QString& output, const rdb::Point& point) {
    if (!output.isEmpty()) output.append(QLatin1Char(' '));
    output.append(QString::number(static_cast<qlonglong>(point.x)));
    output.append(QLatin1Char(' '));
    output.append(QString::number(static_cast<qlonglong>(point.y)));
}

} // namespace

// Table 용도와 빈 Database를 가진 모델을 생성하고 Detail transaction 상태를 초기화한다.
RDBModel::RDBModel(MODEL_TYPE type, QObject* parent)
    : QAbstractTableModel(parent),
      type_(type),
      database_(new rdb::Database),
      exact_result_selection_(false),
      row_offset_(0U),
      detail_load_active_(false),
      loading_check_id_(rdb::invalid_check_id()),
      result_checkpoint_(0U),
      vertex_checkpoint_(0U),
      edge_checkpoint_(0U),
      tagged_value_checkpoint_(0U),
      check_text_checkpoint_(0U),
      loaded_check_checkpoint_(0U),
      interned_tag_checkpoint_(0U) {}

// Qt의 flat table 계약에 따라 현재 offset 이후 표시 가능한 행 수를 반환한다.
int RDBModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : VisibleRowCount(TotalRowCount());
}

// Check/좌표/전체 property 모드에 맞는 동적 열 수를 반환한다.
int RDBModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (type_ == CHIP_TABLE) return CHECK_TABLE_COL_COUNT;
    if (type_ == COORDS_ONLY) return 3;
    const std::size_t count = 3U + active_headers_.size();
    return count > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(count);
}

// 공통 index 범위를 검증한 뒤 Check 또는 Detail 표시 경로로 분기한다.
QVariant RDBModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || modelIndex.row() < 0 ||
        modelIndex.row() >= rowCount() || modelIndex.column() < 0 ||
        modelIndex.column() >= columnCount()) {
        return QVariant();
    }
    return type_ == CHIP_TABLE
        ? CheckIndexData(modelIndex, role)
        : DetailData(modelIndex, role);
}

// 세로 source 번호와 모드별 고정·동적 열 이름을 제공한다.
QVariant RDBModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation == Qt::Vertical) {
        return QVariant::fromValue(
            static_cast<qulonglong>(SourceRow(section) + 1U));
    }
    if (type_ == CHIP_TABLE) {
        if (section == ID) return tr("ID");
        if (section == NAME) return tr("Name");
        if (section == COUNT) return tr("Count");
        if (section == SEEK) return tr("Seek");
        return QVariant();
    }
    if (section == 0) return tr("ID");
    if (section == 1) return tr("Type");
    if (type_ == COORDS_ONLY || section == columnCount() - 1) {
        return tr("Coordinates");
    }
    const int headerIndex = section - 2;
    return headerIndex >= 0 &&
        static_cast<std::size_t>(headerIndex) < active_headers_.size()
        ? QVariant(RDBString(
              database_->strings,
              active_headers_[static_cast<std::size_t>(headerIndex)]))
        : QVariant();
}

// 새 빈 DB를 먼저 확보한 뒤 모델 상태를 한 번의 reset으로 교체한다.
void RDBModel::Clear() {
    // 할당 실패가 beginResetModel 이후에 발생하지 않도록 replacement를 먼저 만든다.
    const RDB_DATABASE_PTR replacement(new rdb::Database);
    CancelCheckLoad();
    beginResetModel();
    database_ = replacement;
    active_check_ids_.clear();
    active_result_indices_.clear();
    active_headers_.clear();
    interned_tag_names_.clear();
    exact_result_selection_ = false;
    row_offset_ = 0U;
    endResetModel();
}

// 모델 종류 변경을 reset 경계로 감싸고 실패 시 이전 종류를 복구한다.
void RDBModel::SetType(MODEL_TYPE type) {
    if (type_ == type) return;
    const MODEL_TYPE previousType = type_;
    beginResetModel();
    try {
        type_ = type;
        RebuildActiveHeaders();
    } catch (...) {
        type_ = previousType;
        endResetModel();
        throw;
    }
    endResetModel();
}

// 현재 TableModel 표시 종류를 반환한다.
RDBModel::MODEL_TYPE RDBModel::GetType() const {
    return type_;
}

// 진행 중 transaction을 취소하고 새 공유 Database를 모델에 연결한다.
void RDBModel::SetDatabase(const RDB_DATABASE_PTR& database) {
    if (!database) throw std::invalid_argument("RDB Database is null");
    CancelCheckLoad();
    beginResetModel();
    database_ = database;
    active_check_ids_.clear();
    active_result_indices_.clear();
    active_headers_.clear();
    interned_tag_names_.clear();
    exact_result_selection_ = false;
    row_offset_ = 0U;
    endResetModel();
}

// 모델들이 공유 중인 Database 소유 포인터를 반환한다.
RDB_DATABASE_PTR RDBModel::GetDatabase() const {
    return database_;
}

// Check 단위 적재 transaction을 열고 모든 append 대상 pool의 checkpoint를 기록한다.
void RDBModel::BeginCheckLoad(rdb::CheckId checkId) {
    if (type_ == CHIP_TABLE) {
        throw std::logic_error("Check table cannot load detail data");
    }
    if (static_cast<std::size_t>(checkId) >=
        database_->rule_checks.size()) {
        throw std::out_of_range("RDB detail CheckId is out of range");
    }
    if (detail_load_active_) {
        throw std::logic_error("Another RDB detail load is already active");
    }

    rdb::RuleCheck& check = database_->check(checkId);
    if (check.detail_loaded) return;
    // checkpoint 할당까지 성공한 후 active flag를 켜야 부분 초기화 상태가 남지 않는다.
    std::unique_ptr<rdb::StringTable::Checkpoint> stringCheckpoint(
        new rdb::StringTable::Checkpoint(database_->strings.checkpoint()));
    original_check_ = check;
    result_checkpoint_ = database_->results.size();
    vertex_checkpoint_ = database_->vertices.size();
    edge_checkpoint_ = database_->edges.size();
    tagged_value_checkpoint_ = database_->tagged_values.size();
    check_text_checkpoint_ = database_->check_text_lines.size();
    loaded_check_checkpoint_ = database_->loaded_rule_check_count;
    interned_tag_checkpoint_ = interned_tag_names_.size();
    string_checkpoint_ = std::move(stringCheckpoint);
    loading_check_id_ = checkId;
    detail_load_active_ = true;

    const bool visible = !exact_result_selection_ &&
        std::find(active_check_ids_.begin(), active_check_ids_.end(), checkId) !=
            active_check_ids_.end();
    if (visible) beginResetModel();
    try {
        check.results = rdb::Range(
            static_cast<rdb::Index>(database_->results.size()), 0U);
        if (visible) {
            row_offset_ = 0U;
            RebuildActiveHeaders();
        }
    } catch (...) {
        if (visible) endResetModel();
        throw;
    }
    if (visible) endResetModel();
}

// Parser 배치를 전역 Database pool의 연속 Range들로 변환해 누적한다.
void RDBModel::AppendCoords(
    rdb::CheckId checkId,
    const RDB_DETAIL_BATCH_PTR& batch) {
    if (!batch || batch->values.empty()) return;
    if (!detail_load_active_ || loading_check_id_ != checkId) {
        throw std::logic_error(
            "RDB detail batch does not match active Check");
    }

    std::vector<rdb::DetailResult>& values = batch->values;
    std::size_t propertyAdd = 0U;
    std::size_t vertexAdd = 0U;
    std::size_t edgeAdd = 0U;
    std::size_t stringAdd = 0U;
    std::size_t stringBytesAdd = 0U;
    bool headersChanged = false;
    const bool visible = !exact_result_selection_ &&
        std::find(active_check_ids_.begin(), active_check_ids_.end(), checkId) !=
            active_check_ids_.end();

    // 실제 쓰기 전에 전체 추가량·문자열 크기·새 header 여부를 한 번 계산한다.
    for (std::size_t i = 0; i < values.size(); ++i) {
        const rdb::DetailResult& source = values[i];
        RequireCapacity(propertyAdd, source.properties.size(),
                        "RDB batch properties");
        RequireCapacity(vertexAdd, source.vertices.size(),
                        "RDB batch vertices");
        RequireCapacity(edgeAdd, source.edges.size(),
                        "RDB batch edges");
        propertyAdd += source.properties.size();
        vertexAdd += source.vertices.size();
        edgeAdd += source.edges.size();
        if (!source.signature_suffix.empty()) {
            ++stringAdd;
            stringBytesAdd += source.signature_suffix.size();
        }
        for (std::size_t property = 0;
             property < source.properties.size(); ++property) {
            const rdb::DetailTag& tag = source.properties[property];
            if (tag.id.empty()) {
                throw std::invalid_argument("RDB detail tag ID is empty");
            }
            ++stringAdd;
            stringBytesAdd += tag.id.size();
            if (!tag.payload.empty()) {
                ++stringAdd;
                stringBytesAdd += tag.payload.size();
            }
            if (visible && type_ == ALL_PARAMS) {
                bool known = false;
                for (std::size_t column = 0;
                     column < active_headers_.size(); ++column) {
                    if (RDBSameText(
                            database_->strings,
                            active_headers_[column], tag.id)) {
                        known = true;
                        break;
                    }
                }
                if (!known) headersChanged = true;
            }
        }
    }

    RequireCapacity(database_->results.size(), values.size(), "RDB results");
    RequireCapacity(database_->tagged_values.size(), propertyAdd,
                    "RDB tagged values");
    RequireCapacity(database_->vertices.size(), vertexAdd, "RDB vertices");
    RequireCapacity(database_->edges.size(), edgeAdd, "RDB edges");
    RequireCapacity(database_->strings.size(), stringAdd,
                    "RDB string records");
    RequireCapacity(database_->strings.byte_size(), stringBytesAdd,
                    "RDB string bytes");

    // reserve를 먼저 끝내면 아래 append 중 재할당 횟수와 부분 실패 가능성이 줄어든다.
    database_->results.reserve(database_->results.size() + values.size());
    database_->tagged_values.reserve(
        database_->tagged_values.size() + propertyAdd);
    database_->vertices.reserve(database_->vertices.size() + vertexAdd);
    database_->edges.reserve(database_->edges.size() + edgeAdd);
    database_->strings.reserve(
        database_->strings.size() + stringAdd,
        database_->strings.byte_size() + stringBytesAdd);
    interned_tag_names_.reserve(interned_tag_names_.size() + propertyAdd);

    const int oldVisibleRows = visible ? rowCount() : 0;
    const int newVisibleRows = VisibleRowCount(
        TotalRowCount() + values.size());
    // 열 구성이 바뀌면 reset, 행만 늘면 insert notification으로 비용을 구분한다.
    const bool resetting = visible && type_ == ALL_PARAMS && headersChanged;
    const bool inserting = visible && !resetting &&
        newVisibleRows > oldVisibleRows;
    if (resetting) beginResetModel();
    if (inserting) {
        beginInsertRows(QModelIndex(), oldVisibleRows, newVisibleRows - 1);
    }

    try {
        for (std::size_t i = 0; i < values.size(); ++i) {
            rdb::DetailResult& source = values[i];
            rdb::Result result;
            result.kind = source.kind;
            result.ordinal = source.ordinal;
            if (!source.signature_suffix.empty()) {
                result.signature_suffix =
                    database_->strings.add(source.signature_suffix);
            }

            // Result에는 vector를 소유시키지 않고 전역 property pool 구간만 기록한다.
            result.properties = rdb::Range(
                static_cast<rdb::Index>(database_->tagged_values.size()),
                static_cast<rdb::Index>(source.properties.size()));
            for (std::size_t property = 0;
                 property < source.properties.size(); ++property) {
                const rdb::DetailTag& sourceTag = source.properties[property];
                const rdb::StringId tagId = InternTagName(sourceTag.id);
                const rdb::StringId payloadId = sourceTag.payload.empty()
                    ? rdb::invalid_string_id()
                    : database_->strings.add(sourceTag.payload);
                database_->tagged_values.push_back(
                    rdb::TaggedValue(tagId, payloadId));
                if (visible && type_ == ALL_PARAMS &&
                    std::find(active_headers_.begin(),
                              active_headers_.end(), tagId) ==
                        active_headers_.end()) {
                    active_headers_.push_back(tagId);
                }
            }

            // 배치가 더 이상 source geometry를 쓰지 않으므로 큰 vector는 move한다.
            if (source.kind == rdb::ResultKind::Polygon) {
                result.geometry = rdb::Range(
                    static_cast<rdb::Index>(database_->vertices.size()),
                    static_cast<rdb::Index>(source.vertices.size()));
                database_->vertices.insert(
                    database_->vertices.end(),
                    std::make_move_iterator(source.vertices.begin()),
                    std::make_move_iterator(source.vertices.end()));
            } else {
                result.geometry = rdb::Range(
                    static_cast<rdb::Index>(database_->edges.size()),
                    static_cast<rdb::Index>(source.edges.size()));
                database_->edges.insert(
                    database_->edges.end(),
                    std::make_move_iterator(source.edges.begin()),
                    std::make_move_iterator(source.edges.end()));
            }
            database_->results.push_back(result);
        }
        database_->check(checkId).results.count = static_cast<rdb::Index>(
            database_->results.size() -
            database_->check(checkId).results.begin);
    } catch (...) {
        if (resetting) endResetModel();
        if (inserting) endInsertRows();
        throw;
    }

    if (resetting) endResetModel();
    if (inserting) endInsertRows();
}

// Index 메타데이터와 파싱 결과를 대조한 뒤 Check transaction을 commit한다.
void RDBModel::FinishCheckLoad(
    rdb::CheckId checkId,
    const rdb::CheckDetail& detail) {
    if (!detail_load_active_ || loading_check_id_ != checkId) {
        throw std::logic_error(
            "RDB detail completion does not match active Check");
    }
    rdb::RuleCheck& check = database_->check(checkId);
    // offset·count·이름 불일치는 다른 Check 결과가 섞였음을 뜻하므로 commit하지 않는다.
    if (!RDBSameText(database_->strings, check.name, detail.name) ||
        check.offset != detail.offset ||
        check.current_result_count != detail.current_result_count ||
        check.original_result_count != detail.original_result_count ||
        check.results.count != detail.current_result_count ||
        detail.check_text.size() != check.declared_check_text_count) {
        throw std::invalid_argument(
            "Parsed RDB detail does not match indexed RuleCheck");
    }

    std::size_t stringBytes = detail.executed_at.size();
    for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
        RequireCapacity(stringBytes, detail.check_text[i].size(),
                        "RDB detail text bytes");
        stringBytes += detail.check_text[i].size();
    }
    RequireCapacity(database_->check_text_lines.size(),
                    detail.check_text.size(), "RDB check text lines");
    RequireCapacity(database_->strings.size(),
                    1U + detail.check_text.size(), "RDB detail strings");
    RequireCapacity(database_->strings.byte_size(), stringBytes,
                    "RDB detail string bytes");

    database_->strings.reserve(
        database_->strings.size() + 1U + detail.check_text.size(),
        database_->strings.byte_size() + stringBytes);
    database_->check_text_lines.reserve(
        database_->check_text_lines.size() + detail.check_text.size());
    check.executed_at = database_->strings.add(detail.executed_at);
    check.check_text = rdb::Range(
        static_cast<rdb::Index>(database_->check_text_lines.size()),
        static_cast<rdb::Index>(detail.check_text.size()));
    for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
        database_->check_text_lines.push_back(
            database_->strings.add(detail.check_text[i]));
    }
    // 모든 문자열·check text 저장이 끝난 뒤 loaded flag와 전역 count를 공개한다.
    check.detail_loaded = true;
    ++database_->loaded_rule_check_count;
    detail_load_active_ = false;
    loading_check_id_ = rdb::invalid_check_id();
    string_checkpoint_.reset();
}

// 활성 Check transaction의 모든 pool과 메타데이터를 checkpoint 시점으로 되돌린다.
void RDBModel::CancelCheckLoad() {
    if (!detail_load_active_) return;
    const bool visible = !exact_result_selection_ &&
        std::find(active_check_ids_.begin(), active_check_ids_.end(),
                  loading_check_id_) != active_check_ids_.end();
    if (visible) beginResetModel();
    try {
        // Result만이 아니라 문자열·geometry·property·header intern 상태를 함께 복구한다.
        database_->strings.rollback(*string_checkpoint_);
        database_->results.resize(result_checkpoint_);
        database_->vertices.resize(vertex_checkpoint_);
        database_->edges.resize(edge_checkpoint_);
        database_->tagged_values.resize(tagged_value_checkpoint_);
        database_->check_text_lines.resize(check_text_checkpoint_);
        database_->loaded_rule_check_count = loaded_check_checkpoint_;
        database_->check(loading_check_id_) = original_check_;
        interned_tag_names_.resize(interned_tag_checkpoint_);
        detail_load_active_ = false;
        loading_check_id_ = rdb::invalid_check_id();
        string_checkpoint_.reset();
        if (visible) {
            row_offset_ = 0U;
            RebuildActiveHeaders();
        }
    } catch (...) {
        if (visible) endResetModel();
        throw;
    }
    if (visible) endResetModel();
}

// 단일 Check 선택을 다중 Check 선택 공통 경로로 변환한다.
void RDBModel::SetActiveCheck(quint64 checkIndex) {
    if (checkIndex >= database_->rule_checks.size()) return;
    std::vector<rdb::CheckId> checkIds(
        1U, static_cast<rdb::CheckId>(checkIndex));
    SetActiveChecks(checkIds);
}

// 여러 Check의 연속 Result Range를 현재 Table source로 선택한다.
void RDBModel::SetActiveChecks(
    const std::vector<rdb::CheckId>& checkIds) {
    for (std::size_t i = 0; i < checkIds.size(); ++i) {
        if (static_cast<std::size_t>(checkIds[i]) >=
            database_->rule_checks.size()) {
            throw std::out_of_range("RDB active CheckId is out of range");
        }
    }
    beginResetModel();
    try {
        active_check_ids_ = checkIds;
        active_result_indices_.clear();
        exact_result_selection_ = false;
        row_offset_ = 0U;
        RebuildActiveHeaders();
    } catch (...) {
        endResetModel();
        throw;
    }
    endResetModel();
}

// Tree node가 가진 정확한 ResultIndex 목록을 현재 Table source로 선택한다.
void RDBModel::SetActiveResults(
    const std::vector<rdb::CheckId>& checkIds,
    const std::vector<rdb::Index>& resultIndices) {
    beginResetModel();
    try {
        active_check_ids_ = checkIds;
        active_result_indices_ = resultIndices;
        exact_result_selection_ = true;
        row_offset_ = 0U;
        RebuildActiveHeaders();
    } catch (...) {
        endResetModel();
        throw;
    }
    endResetModel();
}

// Check Table의 화면 행을 실제 CheckId로 변환한다.
rdb::CheckId RDBModel::CheckIdAt(int row) const {
    if (type_ != CHIP_TABLE || row < 0 || row >= rowCount()) {
        return rdb::invalid_check_id();
    }
    const std::size_t value = SourceRow(row);
    return value < database_->rule_checks.size()
        ? static_cast<rdb::CheckId>(value) : rdb::invalid_check_id();
}

// Detail Table의 화면 행을 선택 방식에 맞는 실제 ResultIndex로 변환한다.
rdb::Index RDBModel::ResultIndexAt(int row) const {
    if (type_ == CHIP_TABLE || row < 0 || row >= rowCount()) {
        return rdb::invalid_index();
    }
    return ResultIndexForSource(SourceRow(row));
}

// exact index 또는 여러 Check Range를 순회해 source 순번의 ResultIndex를 찾는다.
rdb::Index RDBModel::ResultIndexForSource(std::size_t source) const {
    if (exact_result_selection_) {
        return source < active_result_indices_.size()
            ? active_result_indices_[source] : rdb::invalid_index();
    }
    // 전체 ResultIndex 목록을 별도 생성하지 않고 Check Range의 길이만 차감한다.
    for (std::size_t i = 0; i < active_check_ids_.size(); ++i) {
        const rdb::RuleCheck& check = database_->check(active_check_ids_[i]);
        if (source < check.results.count) {
            return static_cast<rdb::Index>(check.results.begin + source);
        }
        source -= check.results.count;
    }
    return rdb::invalid_index();
}

// 지금까지 intern된 property 이름을 원래 발견 순서대로 반환한다.
QStringList RDBModel::AvailableHeaders() const {
    QStringList headers;
    for (std::size_t i = 0; i < interned_tag_names_.size(); ++i) {
        const QString value = RDBString(
            database_->strings, interned_tag_names_[i]);
        if (!value.isEmpty()) headers.append(value);
    }
    return headers;
}

// 현재 Database의 좌표 정밀도(DBU)를 반환한다.
double RDBModel::DBU() const {
    return database_->database_precision;
}

// 현재 Database의 Top Cell 이름을 QString으로 반환한다.
QString RDBModel::TopCell() const {
    return RDBString(database_->strings, database_->top_cell_name);
}

// 표시 제한 전의 전체 source 행 수를 overflow 없이 계산한다.
std::size_t RDBModel::TotalRowCount() const {
    if (type_ == CHIP_TABLE) return database_->rule_checks.size();
    if (exact_result_selection_) return active_result_indices_.size();
    std::size_t total = 0U;
    for (std::size_t i = 0; i < active_check_ids_.size(); ++i) {
        const std::size_t count = database_->check(
            active_check_ids_[i]).results.count;
        if (count > std::numeric_limits<std::size_t>::max() - total) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += count;
    }
    return total;
}

// Qt int 행 한계를 넘는 데이터에서 현재 window 시작 offset을 반환한다.
std::size_t RDBModel::RowOffset() const {
    return row_offset_;
}

// 전체 행 범위 안으로 보정한 window offset을 모델 reset으로 적용한다.
void RDBModel::SetRowOffset(std::size_t offset) {
    const std::size_t bounded = std::min(offset, TotalRowCount());
    if (bounded == row_offset_) return;
    beginResetModel();
    row_offset_ = bounded;
    endResetModel();
}

// 현재 offset 이후 행 수를 Qt가 표현 가능한 int 최댓값으로 제한한다.
int RDBModel::VisibleRowCount(std::size_t totalRows) const {
    if (row_offset_ >= totalRows) return 0;
    const std::size_t remaining = totalRows - row_offset_;
    return remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(remaining);
}

// 화면의 int 행 번호에 64-bit window offset을 더해 source 순번을 만든다.
std::size_t RDBModel::SourceRow(int modelRow) const {
    return row_offset_ + static_cast<std::size_t>(modelRow);
}

// 화면 행이 가리키는 유효 Result 주소를 반환하고 범위 밖이면 null을 반환한다.
const rdb::Result* RDBModel::ResultAt(int modelRow) const {
    if (type_ == CHIP_TABLE || modelRow < 0 || modelRow >= rowCount()) {
        return 0;
    }
    const rdb::Index index = ResultIndexAt(modelRow);
    return index != rdb::invalid_index() &&
        static_cast<std::size_t>(index) < database_->results.size()
        ? &database_->results[static_cast<std::size_t>(index)] : 0;
}

// 활성 Result에 실제 존재하는 property ID만 발견 순서대로 다시 수집한다.
void RDBModel::RebuildActiveHeaders() {
    if (type_ != ALL_PARAMS) {
        active_headers_.clear();
        return;
    }
    // 임시 vector 완성 후 swap해 예외 시 기존 header 목록을 보존한다.
    std::vector<rdb::StringId> rebuiltHeaders;
    const std::size_t maximum = std::min(
        TotalRowCount(),
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const auto collectHeaders =
        [this, &rebuiltHeaders](rdb::Index resultIndex) {
        const rdb::Result* result =
            resultIndex != rdb::invalid_index() &&
            static_cast<std::size_t>(resultIndex) < database_->results.size()
            ? &database_->results[static_cast<std::size_t>(resultIndex)] : 0;
        if (!result) return;
        for (std::size_t property = 0;
             property < result->properties.count; ++property) {
            const rdb::StringId id = database_->tagged_values[
                static_cast<std::size_t>(result->properties.begin) +
                property].id;
            if (std::find(rebuiltHeaders.begin(), rebuiltHeaders.end(), id) ==
                rebuiltHeaders.end()) {
                rebuiltHeaders.push_back(id);
            }
        }
    };

    std::size_t visited = 0U;
    // exact 선택은 index vector를, Check 선택은 DB Range를 직접 순회한다.
    if (exact_result_selection_) {
        for (std::size_t i = 0;
             i < active_result_indices_.size() && visited < maximum;
             ++i, ++visited) {
            collectHeaders(active_result_indices_[i]);
        }
        active_headers_.swap(rebuiltHeaders);
        return;
    }

    for (std::size_t checkNumber = 0;
         checkNumber < active_check_ids_.size() && visited < maximum;
         ++checkNumber) {
        const rdb::RuleCheck& check = database_->check(
            active_check_ids_[checkNumber]);
        for (std::size_t offset = 0;
             offset < check.results.count && visited < maximum;
             ++offset, ++visited) {
            collectHeaders(static_cast<rdb::Index>(
                static_cast<std::size_t>(check.results.begin) + offset));
        }
    }
    active_headers_.swap(rebuiltHeaders);
}

// 같은 property 이름은 StringTable ID 하나를 재사용하고 새 이름만 추가한다.
rdb::StringId RDBModel::InternTagName(const std::string& name) {
    for (std::size_t i = 0; i < interned_tag_names_.size(); ++i) {
        if (RDBSameText(database_->strings, interned_tag_names_[i], name)) {
            return interned_tag_names_[i];
        }
    }
    const rdb::StringId id = database_->strings.add(name);
    interned_tag_names_.push_back(id);
    return id;
}

// Check Table의 ID·이름·개수·offset과 이름 tooltip을 Database에서 읽는다.
QVariant RDBModel::CheckIndexData(
    const QModelIndex& modelIndex,
    int role) const {
    const std::size_t source = SourceRow(modelIndex.row());
    if (source >= database_->rule_checks.size()) return QVariant();
    const rdb::RuleCheck& check = database_->rule_checks[source];
    if (role == Qt::ToolTipRole && modelIndex.column() == NAME) {
        return RDBString(database_->strings, check.comment);
    }
    if (role != Qt::DisplayRole) return QVariant();
    if (modelIndex.column() == ID) {
        return QVariant::fromValue(static_cast<qulonglong>(source));
    }
    if (modelIndex.column() == NAME) {
        return RDBString(database_->strings, check.name);
    }
    if (modelIndex.column() == COUNT) {
        return QVariant::fromValue(
            static_cast<qulonglong>(check.current_result_count));
    }
    if (modelIndex.column() == SEEK) {
        return QVariant::fromValue(static_cast<qulonglong>(check.offset));
    }
    return QVariant();
}

// Detail Result의 고정 열과 동적 property 열 값을 필요할 때 계산한다.
QVariant RDBModel::DetailData(
    const QModelIndex& modelIndex,
    int role) const {
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return QVariant();
    }
    const rdb::Result* result = ResultAt(modelIndex.row());
    if (!result) return QVariant();
    if (modelIndex.column() == 0) {
        return QVariant::fromValue(
            static_cast<qulonglong>(ResultIndexAt(modelIndex.row())));
    }
    if (modelIndex.column() == 1) {
        return result->kind == rdb::ResultKind::Polygon
            ? QStringLiteral("p") : QStringLiteral("e");
    }
    if (type_ == COORDS_ONLY ||
        modelIndex.column() == columnCount() - 1) {
        return CoordinateText(*result);
    }

    const std::size_t headerIndex =
        static_cast<std::size_t>(modelIndex.column() - 2);
    if (headerIndex >= active_headers_.size()) return QVariant();
    const rdb::StringId requested = active_headers_[headerIndex];
    // 같은 ID가 여러 번 있으면 정보 손실 없이 줄바꿈 문자열로 모두 표시한다.
    QStringList values;
    for (std::size_t property = 0;
         property < result->properties.count; ++property) {
        const rdb::TaggedValue& value = database_->tagged_values[
            static_cast<std::size_t>(result->properties.begin) + property];
        if (value.id == requested) {
            values.append(RDBString(database_->strings, value.payload));
        }
    }
    if (values.size() == 1) return PayloadVariant(values.front());
    return values.join(QStringLiteral("\n"));
}

// 전역 geometry Range를 기존 공백 구분 좌표 문자열로 지연 변환한다.
QString RDBModel::CoordinateText(const rdb::Result& value) const {
    QString output;
    if (value.kind == rdb::ResultKind::Polygon) {
        for (std::size_t i = 0; i < value.geometry.count; ++i) {
            AppendPoint(output, database_->vertices[
                static_cast<std::size_t>(value.geometry.begin) + i]);
        }
    } else {
        for (std::size_t i = 0; i < value.geometry.count; ++i) {
            const rdb::Edge& edge = database_->edges[
                static_cast<std::size_t>(value.geometry.begin) + i];
            AppendPoint(output, edge.first);
            AppendPoint(output, edge.second);
        }
    }
    return output;
}
