#include "rdb_table_model.hpp"

#include <QVariant>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

const std::size_t databaseIndexMax =
    static_cast<std::size_t>(std::numeric_limits<rdb::Index>::max());

void RequireDatabaseCapacity(
    std::size_t current,
    std::size_t added,
    const char* description) {
    if (added > databaseIndexMax || current > databaseIndexMax - added) {
        throw std::length_error(
            std::string(description) + " exceeds rdb::Database capacity");
    }
}

bool SameText(
    const rdb::StringTable& strings,
    rdb::StringId id,
    const std::string& value) {
    const rdb::StringRef stored = strings.get(id);
    return stored.size == value.size() &&
        (stored.size == 0 ||
         std::memcmp(stored.data, value.data(), stored.size) == 0);
}

QString ToQString(const rdb::StringTable& strings, rdb::StringId id) {
    const rdb::StringRef text = strings.get(id);
    return text.data
        ? QString::fromUtf8(text.data, static_cast<int>(text.size))
        : QString();
}

void AppendPoint(QString& output, const rdb::Point& point) {
    if (!output.isEmpty()) output.append(QLatin1Char(' '));
    output.append(QString::number(static_cast<qlonglong>(point.x)));
    output.append(QLatin1Char(' '));
    output.append(QString::number(static_cast<qlonglong>(point.y)));
}

std::shared_ptr<rdb::Database> DatabaseFromIndex(
    const rdb::CheckIndexDatabase& index) {
    if (index.checks.size() >= databaseIndexMax) {
        throw std::length_error(
            "RDB check list exceeds rdb::Database capacity");
    }

    std::shared_ptr<rdb::Database> database(new rdb::Database);
    database->database_precision = index.database_precision;
    database->strings.reserve(
        1U + index.checks.size() * 2U,
        index.top_cell_name.size());
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

} // namespace

RdbTableModel::RdbTableModel(ModelType type, QObject* parent)
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

// 실제 데이터 개수는 size_t로 유지하고 Qt View에는 int 범위만 노출한다.
int RdbTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : VisibleRowCount(TotalRowCount());
}

int RdbTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (type_ == CheckIndex) return CheckColumnCount;
    if (type_ == CoordinatesOnly) return 1;
    return tagged_value_columns_.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(tagged_value_columns_.size());
}

QVariant RdbTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() ||
        index.row() < 0 || index.row() >= rowCount() ||
        index.column() < 0 || index.column() >= columnCount()) {
        return QVariant();
    }
    return type_ == CheckIndex
        ? CheckIndexData(index, role)
        : DetailData(index, role);
}

QVariant RdbTableModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation == Qt::Vertical) {
        return QVariant::fromValue(
            static_cast<qulonglong>(SourceRow(section) + 1U));
    }

    if (type_ == CheckIndex) {
        if (section == CheckNameColumn) return tr("Check Name");
        if (section == ResultCountColumn) return tr("Result Count");
        return QVariant();
    }
    if (type_ == CoordinatesOnly) {
        return section == 0 ? QVariant(tr("Coords")) : QVariant();
    }
    if (section < 0 ||
        static_cast<std::size_t>(section) >= tagged_value_columns_.size()) {
        return QVariant();
    }
    return ToQString(
        database_->strings,
        tagged_value_columns_[static_cast<std::size_t>(section)]);
}

RdbTableModel::ModelType RdbTableModel::GetModelType() const {
    return type_;
}

void RdbTableModel::SetCheckIndexDatabase(rdb::CheckIndexDatabase index) {
    SetDatabase(CheckIndex, DatabaseFromIndex(index));
}

void RdbTableModel::SetDatabase(
    ModelType type,
    const std::shared_ptr<rdb::Database>& database) {
    if (!database) throw std::invalid_argument("RDB Database is null");
    CancelDetailLoad();

    const bool sameDatabase = database_ == database;
    beginResetModel();
    type_ = type;
    database_ = database;
    selected_check_ids_.clear();
    selected_result_indices_.clear();
    exact_result_selection_ = false;
    tagged_value_columns_.clear();
    if (!sameDatabase) interned_tag_names_.clear();
    row_offset_ = 0U;
    endResetModel();
}

std::shared_ptr<rdb::Database> RdbTableModel::GetDatabase() const {
    return database_;
}

rdb::CheckId RdbTableModel::CheckIdAt(int modelRow) const {
    if (type_ != CheckIndex || modelRow < 0 || modelRow >= rowCount()) {
        return rdb::invalid_check_id();
    }
    const std::size_t id = SourceRow(modelRow);
    return id >= static_cast<std::size_t>(rdb::invalid_check_id())
        ? rdb::invalid_check_id()
        : static_cast<rdb::CheckId>(id);
}

void RdbTableModel::SelectDetailCheck(rdb::CheckId checkId) {
    std::vector<rdb::CheckId> checkIds;
    if (checkId != rdb::invalid_check_id()) checkIds.push_back(checkId);
    SelectDetailChecks(checkIds);
}

void RdbTableModel::SelectDetailChecks(
    const std::vector<rdb::CheckId>& checkIds) {
    if (type_ == CheckIndex) {
        throw std::logic_error("Check-index model cannot select detail data");
    }
    std::vector<rdb::CheckId> uniqueIds;
    for (std::size_t i = 0; i < checkIds.size(); ++i) {
        const rdb::CheckId checkId = checkIds[i];
        if (static_cast<std::size_t>(checkId) >=
            database_->rule_checks.size()) {
            throw std::out_of_range("RDB detail CheckId is out of range");
        }
        if (std::find(uniqueIds.begin(), uniqueIds.end(), checkId) ==
            uniqueIds.end()) {
            uniqueIds.push_back(checkId);
        }
    }

    beginResetModel();
    selected_check_ids_.swap(uniqueIds);
    selected_result_indices_.clear();
    exact_result_selection_ = false;
    row_offset_ = 0U;
    RebuildTagColumns();
    endResetModel();
}

void RdbTableModel::SelectDetailResults(
    const std::vector<rdb::CheckId>& checkIds,
    const std::vector<rdb::Index>& resultIndices) {
    if (type_ == CheckIndex) {
        throw std::logic_error("Check-index model cannot select detail data");
    }
    for (std::size_t i = 0; i < checkIds.size(); ++i) {
        if (static_cast<std::size_t>(checkIds[i]) >=
            database_->rule_checks.size()) {
            throw std::out_of_range("RDB detail CheckId is out of range");
        }
    }
    for (std::size_t i = 0; i < resultIndices.size(); ++i) {
        if (static_cast<std::size_t>(resultIndices[i]) >=
            database_->results.size()) {
            throw std::out_of_range("RDB detail Result index is out of range");
        }
    }

    beginResetModel();
    selected_check_ids_ = checkIds;
    selected_result_indices_ = resultIndices;
    exact_result_selection_ = true;
    row_offset_ = 0U;
    RebuildTagColumns();
    endResetModel();
}

rdb::CheckId RdbTableModel::SelectedCheckId() const {
    return selected_check_ids_.size() == 1U
        ? selected_check_ids_[0]
        : rdb::invalid_check_id();
}

const std::vector<rdb::CheckId>&
RdbTableModel::GetSelectedCheckIds() const {
    return selected_check_ids_;
}

bool RdbTableModel::IsCheckSelected(rdb::CheckId checkId) const {
    return std::find(
        selected_check_ids_.begin(), selected_check_ids_.end(), checkId) !=
        selected_check_ids_.end();
}

bool RdbTableModel::IsSelectedDetailLoaded() const {
    if (selected_check_ids_.empty()) return false;
    for (std::size_t i = 0; i < selected_check_ids_.size(); ++i) {
        if (!database_->check(selected_check_ids_[i]).detail_loaded) {
            return false;
        }
    }
    return true;
}

bool RdbTableModel::IsDetailLoadActive() const {
    return detail_load_active_;
}

rdb::CheckId RdbTableModel::LoadingCheckId() const {
    return loading_check_id_;
}

void RdbTableModel::BeginDetailLoad(rdb::CheckId checkId) {
    // Batch 저장 실패 시 모든 STL Pool을 시작 시점으로 되돌릴 수 있게 기록한다.
    if (type_ == CheckIndex) {
        throw std::logic_error("Check-index model cannot load detail data");
    }
    if (static_cast<std::size_t>(checkId) >= database_->rule_checks.size()) {
        throw std::out_of_range("RDB detail CheckId is out of range");
    }
    if (detail_load_active_) {
        throw std::logic_error("Another RDB detail load is already active");
    }

    rdb::RuleCheck& check = database_->check(checkId);
    if (check.detail_loaded) return;
    if (database_->results.size() >= databaseIndexMax) {
        throw std::length_error("RDB result pool has no remaining capacity");
    }

    detail_load_active_ = true;
    loading_check_id_ = checkId;
    string_checkpoint_.reset(new rdb::StringTable::Checkpoint(
        database_->strings.checkpoint()));
    original_check_ = check;
    result_checkpoint_ = database_->results.size();
    vertex_checkpoint_ = database_->vertices.size();
    edge_checkpoint_ = database_->edges.size();
    tagged_value_checkpoint_ = database_->tagged_values.size();
    check_text_checkpoint_ = database_->check_text_lines.size();
    loaded_check_checkpoint_ = database_->loaded_rule_check_count;
    interned_tag_checkpoint_ = interned_tag_names_.size();

    const bool visible = !exact_result_selection_ && IsCheckSelected(checkId);
    if (visible) beginResetModel();
    check.results = rdb::Range(
        static_cast<rdb::Index>(database_->results.size()), 0U);
    if (visible) {
        row_offset_ = 0U;
        RebuildTagColumns();
        endResetModel();
    }
}

void RdbTableModel::AppendDetailResults(
    std::vector<rdb::DetailResult> results) {
    // 작업 스레드가 전달한 결과를 GUI 스레드에서 Database Pool에 반영한다.
    if (results.empty()) return;
    if (!detail_load_active_) {
        throw std::logic_error("No active RDB detail load transaction");
    }

    const bool visible =
        !exact_result_selection_ && IsCheckSelected(loading_check_id_);
    std::size_t propertyAdd = 0U;
    std::size_t vertexAdd = 0U;
    std::size_t edgeAdd = 0U;
    std::size_t stringAdd = 0U;
    std::size_t stringBytesAdd = 0U;
    bool headersChanged = false;

    for (std::size_t i = 0; i < results.size(); ++i) {
        const rdb::DetailResult& source = results[i];
        RequireDatabaseCapacity(propertyAdd, source.properties.size(),
                                "RDB batch properties");
        RequireDatabaseCapacity(vertexAdd, source.vertices.size(),
                                "RDB batch vertices");
        RequireDatabaseCapacity(edgeAdd, source.edges.size(),
                                "RDB batch edges");
        propertyAdd += source.properties.size();
        vertexAdd += source.vertices.size();
        edgeAdd += source.edges.size();
        if (!source.signature_suffix.empty()) {
            ++stringAdd;
            stringBytesAdd += source.signature_suffix.size();
        }
        for (std::size_t property = 0;
             property < source.properties.size();
             ++property) {
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
            if (visible && type_ == AllParameters) {
                bool isColumn = false;
                for (std::size_t column = 0;
                     column < tagged_value_columns_.size();
                     ++column) {
                    if (SameText(database_->strings,
                                 tagged_value_columns_[column], tag.id)) {
                        isColumn = true;
                        break;
                    }
                }
                if (!isColumn) headersChanged = true;
            }
        }
    }

    RequireDatabaseCapacity(database_->results.size(), results.size(),
                            "RDB results");
    RequireDatabaseCapacity(database_->tagged_values.size(), propertyAdd,
                            "RDB tagged values");
    RequireDatabaseCapacity(database_->vertices.size(), vertexAdd,
                            "RDB vertices");
    RequireDatabaseCapacity(database_->edges.size(), edgeAdd,
                            "RDB edges");
    RequireDatabaseCapacity(database_->strings.size(), stringAdd,
                            "RDB string records");
    RequireDatabaseCapacity(database_->strings.byte_size(), stringBytesAdd,
                            "RDB string bytes");

    database_->results.reserve(database_->results.size() + results.size());
    database_->tagged_values.reserve(
        database_->tagged_values.size() + propertyAdd);
    database_->vertices.reserve(database_->vertices.size() + vertexAdd);
    database_->edges.reserve(database_->edges.size() + edgeAdd);
    database_->strings.reserve(
        database_->strings.size() + stringAdd,
        database_->strings.byte_size() + stringBytesAdd);
    interned_tag_names_.reserve(interned_tag_names_.size() + propertyAdd);
    tagged_value_columns_.reserve(tagged_value_columns_.size() + propertyAdd);

    const int oldVisibleRows = visible ? rowCount() : 0;
    const std::size_t newTotal = TotalRowCount() + results.size();
    const int newVisibleRows = VisibleRowCount(newTotal);
    const bool resetting =
        visible &&
        (selected_check_ids_.size() > 1U ||
         (type_ == AllParameters && headersChanged));
    const bool inserting =
        visible && !resetting && newVisibleRows > oldVisibleRows;
    if (resetting) beginResetModel();
    if (inserting) {
        beginInsertRows(QModelIndex(), oldVisibleRows, newVisibleRows - 1);
    }

    try {
        for (std::size_t i = 0; i < results.size(); ++i) {
            rdb::DetailResult& source = results[i];
            rdb::Result result;
            result.kind = source.kind;
            result.ordinal = source.ordinal;
            if (!source.signature_suffix.empty()) {
                result.signature_suffix =
                    database_->strings.add(source.signature_suffix);
            }

            result.properties = rdb::Range(
                static_cast<rdb::Index>(database_->tagged_values.size()),
                static_cast<rdb::Index>(source.properties.size()));
            for (std::size_t property = 0;
                 property < source.properties.size();
                 ++property) {
                const rdb::DetailTag& sourceTag = source.properties[property];
                const rdb::StringId tagId = InternTagName(sourceTag.id);
                const rdb::StringId payloadId = sourceTag.payload.empty()
                    ? rdb::invalid_string_id()
                    : database_->strings.add(sourceTag.payload);
                database_->tagged_values.push_back(
                    rdb::TaggedValue(tagId, payloadId));
                if (visible && type_ == AllParameters &&
                    std::find(tagged_value_columns_.begin(),
                              tagged_value_columns_.end(), tagId) ==
                        tagged_value_columns_.end()) {
                    tagged_value_columns_.push_back(tagId);
                }
            }

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

        rdb::RuleCheck& check = database_->check(loading_check_id_);
        check.results.count = static_cast<rdb::Index>(
            database_->results.size() - check.results.begin);
    } catch (...) {
        if (resetting) endResetModel();
        if (inserting) endInsertRows();
        throw;
    }

    if (resetting) endResetModel();
    if (inserting) endInsertRows();
}

void RdbTableModel::FinishDetailLoad(const rdb::CheckDetail& detail) {
    if (!detail_load_active_) {
        throw std::logic_error("No active RDB detail load to finish");
    }
    rdb::RuleCheck& check = database_->check(loading_check_id_);
    if (!SameText(database_->strings, check.name, detail.name) ||
        check.offset != detail.offset ||
        check.current_result_count != detail.current_result_count ||
        check.original_result_count != detail.original_result_count ||
        check.results.count != detail.current_result_count ||
        detail.check_text.size() != check.declared_check_text_count) {
        throw std::invalid_argument(
            "Parsed RDB detail does not match its indexed RuleCheck");
    }

    std::size_t stringBytes = detail.executed_at.size();
    for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
        RequireDatabaseCapacity(stringBytes, detail.check_text[i].size(),
                                "RDB detail text bytes");
        stringBytes += detail.check_text[i].size();
    }
    RequireDatabaseCapacity(
        database_->check_text_lines.size(), detail.check_text.size(),
        "RDB check text lines");
    RequireDatabaseCapacity(
        database_->strings.size(), 1U + detail.check_text.size(),
        "RDB detail strings");
    RequireDatabaseCapacity(
        database_->strings.byte_size(), stringBytes,
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
    check.detail_loaded = true;
    ++database_->loaded_rule_check_count;

    detail_load_active_ = false;
    loading_check_id_ = rdb::invalid_check_id();
    string_checkpoint_.reset();
}

void RdbTableModel::CancelDetailLoad() {
    // 부분 반영된 결과와 문자열을 Checkpoint 기준으로 함께 롤백한다.
    if (!detail_load_active_) return;

    const bool visible =
        !exact_result_selection_ && IsCheckSelected(loading_check_id_);
    if (visible) beginResetModel();
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
        RebuildTagColumns();
        endResetModel();
    }
}

std::size_t RdbTableModel::TotalRowCount() const {
    if (type_ == CheckIndex) return database_->rule_checks.size();
    if (exact_result_selection_) return selected_result_indices_.size();

    std::size_t total = 0U;
    for (std::size_t i = 0; i < selected_check_ids_.size(); ++i) {
        const std::size_t count = static_cast<std::size_t>(
            database_->check(selected_check_ids_[i]).results.count);
        if (count > std::numeric_limits<std::size_t>::max() - total) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += count;
    }
    return total;
}

std::size_t RdbTableModel::RowOffset() const {
    return row_offset_;
}

void RdbTableModel::SetRowOffset(std::size_t offset) {
    const std::size_t boundedOffset = std::min(offset, TotalRowCount());
    if (boundedOffset == row_offset_) return;
    beginResetModel();
    row_offset_ = boundedOffset;
    endResetModel();
}

int RdbTableModel::VisibleRowCount(std::size_t totalRows) const {
    if (row_offset_ >= totalRows) return 0;
    const std::size_t remaining = totalRows - row_offset_;
    return remaining >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(remaining);
}

std::size_t RdbTableModel::SourceRow(int modelRow) const {
    return row_offset_ + static_cast<std::size_t>(modelRow);
}

const rdb::RuleCheck* RdbTableModel::SelectedCheck() const {
    if (selected_check_ids_.empty() ||
        static_cast<std::size_t>(selected_check_ids_[0]) >=
            database_->rule_checks.size()) {
        return 0;
    }
    return &database_->rule_checks[selected_check_ids_[0]];
}

const rdb::Result* RdbTableModel::DetailResultAt(int modelRow) const {
    if (modelRow < 0 || modelRow >= rowCount()) return 0;
    std::size_t source = SourceRow(modelRow);
    if (exact_result_selection_) {
        if (source >= selected_result_indices_.size()) return 0;
        const std::size_t resultIndex = static_cast<std::size_t>(
            selected_result_indices_[source]);
        return resultIndex < database_->results.size()
            ? &database_->results[resultIndex]
            : 0;
    }

    for (std::size_t i = 0; i < selected_check_ids_.size(); ++i) {
        const rdb::RuleCheck& check = database_->check(selected_check_ids_[i]);
        const std::size_t count = static_cast<std::size_t>(check.results.count);
        if (source < count) {
            const std::size_t resultIndex =
                static_cast<std::size_t>(check.results.begin) + source;
            return resultIndex < database_->results.size()
                ? &database_->results[resultIndex]
                : 0;
        }
        source -= count;
    }
    return 0;
}

void RdbTableModel::RebuildTagColumns() {
    tagged_value_columns_.clear();
    if (type_ != AllParameters) return;

    const std::size_t total = std::min(
        TotalRowCount(),
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
    for (std::size_t resultOffset = 0; resultOffset < total; ++resultOffset) {
        const rdb::Result* const resultPointer = DetailResultAt(
            static_cast<int>(resultOffset));
        if (!resultPointer) continue;
        const rdb::Result& result = *resultPointer;
        for (std::size_t propertyOffset = 0;
             propertyOffset <
                static_cast<std::size_t>(result.properties.count);
             ++propertyOffset) {
            const rdb::StringId tagId = database_->tagged_values[
                static_cast<std::size_t>(result.properties.begin) +
                propertyOffset].id;
            if (std::find(tagged_value_columns_.begin(),
                          tagged_value_columns_.end(), tagId) ==
                tagged_value_columns_.end()) {
                tagged_value_columns_.push_back(tagId);
            }
        }
    }
}

rdb::StringId RdbTableModel::InternTagName(const std::string& name) {
    for (std::size_t i = 0; i < interned_tag_names_.size(); ++i) {
        if (SameText(database_->strings, interned_tag_names_[i], name)) {
            return interned_tag_names_[i];
        }
    }
    const rdb::StringId id = database_->strings.add(name);
    interned_tag_names_.push_back(id);
    return id;
}

QVariant RdbTableModel::CheckIndexData(
    const QModelIndex& index,
    int role) const {
    const rdb::RuleCheck& check =
        database_->rule_checks[SourceRow(index.row())];
    if (role == Qt::DisplayRole) {
        if (index.column() == CheckNameColumn) {
            return ToQString(database_->strings, check.name);
        }
        if (index.column() == ResultCountColumn) {
            return QVariant::fromValue(
                static_cast<qulonglong>(check.current_result_count));
        }
    }
    if (role == Qt::ToolTipRole && index.column() == CheckNameColumn) {
        return ToQString(database_->strings, check.comment);
    }
    if (role == Qt::TextAlignmentRole &&
        index.column() == ResultCountColumn) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant RdbTableModel::DetailData(
    const QModelIndex& index,
    int role) const {
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return QVariant();
    }
    const rdb::Result* result = DetailResultAt(index.row());
    if (!result) return QVariant();

    if (type_ == CoordinatesOnly) {
        QString coordinates;
        if (result->kind == rdb::ResultKind::Polygon) {
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(result->geometry.count);
                 ++i) {
                AppendPoint(
                    coordinates,
                    database_->vertices[
                        static_cast<std::size_t>(result->geometry.begin) + i]);
            }
        } else {
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(result->geometry.count);
                 ++i) {
                const rdb::Edge& edge = database_->edges[
                    static_cast<std::size_t>(result->geometry.begin) + i];
                AppendPoint(coordinates, edge.first);
                AppendPoint(coordinates, edge.second);
            }
        }
        return coordinates;
    }

    const rdb::StringId requestedTag =
        tagged_value_columns_[static_cast<std::size_t>(index.column())];
    QString values;
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(result->properties.count);
         ++i) {
        const rdb::TaggedValue& taggedValue = database_->tagged_values[
            static_cast<std::size_t>(result->properties.begin) + i];
        if (taggedValue.id == requestedTag) {
            if (!values.isEmpty()) values.append(QLatin1Char('\n'));
            values.append(ToQString(database_->strings, taggedValue.payload));
        }
    }
    return values;
}
