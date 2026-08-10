#include "rdb_model.hpp"

#include <QStringList>

#include <limits>

namespace {

int BoundedRowCount(std::size_t size) {
    return size > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(size);
}

} // namespace

RDBModel::RDBModel(MODEL_TYPE type, QObject* parent)
    : QAbstractTableModel(parent),
      type_(type),
      dbu_(0.0),
      active_check_index_(0U),
      has_active_check_(false) {}

int RDBModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return type_ == CHIP_TABLE
        ? BoundedRowCount(chip_list_.size())
        : BoundedRowCount(active_coords_.size());
}

int RDBModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (type_ == CHIP_TABLE) return CHECK_TABLE_COL_COUNT;
    if (type_ == COORDS_ONLY) return 3;
    return 3 + active_headers_.size();
}

QVariant RDBModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || modelIndex.row() < 0 ||
        modelIndex.row() >= rowCount() || modelIndex.column() < 0 ||
        modelIndex.column() >= columnCount()) {
        return QVariant();
    }

    if (type_ == CHIP_TABLE) {
        const RDB_DATA_PTR value = chip_list_[
            static_cast<std::size_t>(modelIndex.row())];
        if (!value) return QVariant();
        if (role == Qt::ToolTipRole && modelIndex.column() == NAME) {
            return value->comment;
        }
        if (role != Qt::DisplayRole) return QVariant();
        if (modelIndex.column() == ID) return QVariant::fromValue(value->index);
        if (modelIndex.column() == NAME) return value->name;
        if (modelIndex.column() == COUNT) return QVariant::fromValue(value->count);
        if (modelIndex.column() == SEEK) return QVariant::fromValue(value->seek_point);
        return QVariant();
    }

    const RDB_ALL_DATA_PTR value = active_coords_[
        static_cast<std::size_t>(modelIndex.row())];
    if (!value || (role != Qt::DisplayRole && role != Qt::ToolTipRole)) {
        return QVariant();
    }
    if (modelIndex.column() == 0) return QVariant::fromValue(value->index);
    if (modelIndex.column() == 1) {
        return QString(QChar::fromLatin1(value->type));
    }
    if (type_ == COORDS_ONLY || modelIndex.column() == columnCount() - 1) {
        return CoordinateText(*value);
    }
    const int headerIndex = modelIndex.column() - 2;
    return headerIndex >= 0 && headerIndex < active_headers_.size()
        ? value->PropertyValue(active_headers_[headerIndex]) : QVariant();
}

QVariant RDBModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation == Qt::Vertical) return section + 1;
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
    return headerIndex >= 0 && headerIndex < active_headers_.size()
        ? QVariant(active_headers_[headerIndex]) : QVariant();
}

void RDBModel::Clear() {
    beginResetModel();
    dbu_ = 0.0;
    topcell_.clear();
    chip_list_.clear();
    coords_list_.clear();
    coords_map_.clear();
    header_map_.clear();
    active_coords_.clear();
    active_headers_.clear();
    active_check_index_ = 0U;
    has_active_check_ = false;
    endResetModel();
}

void RDBModel::SetType(MODEL_TYPE type) {
    if (type_ == type) return;
    beginResetModel();
    type_ = type;
    endResetModel();
}

RDBModel::MODEL_TYPE RDBModel::GetType() const {
    return type_;
}

void RDBModel::SetIndexResult(const RDB_INDEX_RESULT_PTR& result) {
    beginResetModel();
    chip_list_ = result ? result->chips : RDB_DATA_LIST();
    dbu_ = result ? result->dbu : 0.0;
    topcell_ = result ? result->topcell : QString();
    coords_list_.clear();
    coords_map_.clear();
    header_map_.clear();
    active_coords_.clear();
    active_headers_.clear();
    has_active_check_ = false;
    endResetModel();
}

void RDBModel::AppendCoords(
    quint64 checkIndex,
    const RDB_ALL_DATA_LIST& values,
    const QStringList& headers) {
    if (values.empty()) {
        QStringList& storedHeaders = header_map_[checkIndex];
        MergeHeaders(storedHeaders, headers);
        return;
    }

    RDB_ALL_DATA_LIST& byCheck = coords_map_[checkIndex];
    const bool visible = has_active_check_ &&
        active_check_index_ == checkIndex;
    const int oldVisibleRows = visible
        ? BoundedRowCount(active_coords_.size()) : 0;

    QStringList mergedHeaders = header_map_.value(checkIndex);
    const int previousHeaderCount = mergedHeaders.size();
    MergeHeaders(mergedHeaders, headers);
    header_map_[checkIndex] = mergedHeaders;
    const bool headersChanged = visible && type_ == ALL_PARAMS &&
        mergedHeaders.size() != previousHeaderCount;

    if (headersChanged) beginResetModel();
    int newVisibleRows = oldVisibleRows;
    if (visible && !headersChanged) {
        const std::size_t available =
            std::numeric_limits<std::size_t>::max() - active_coords_.size();
        const std::size_t newSize = values.size() > available
            ? std::numeric_limits<std::size_t>::max()
            : active_coords_.size() + values.size();
        newVisibleRows = BoundedRowCount(newSize);
        if (newVisibleRows > oldVisibleRows) {
            beginInsertRows(
                QModelIndex(), oldVisibleRows, newVisibleRows - 1);
        }
    }

    coords_list_.insert(coords_list_.end(), values.begin(), values.end());
    byCheck.insert(byCheck.end(), values.begin(), values.end());
    if (visible) {
        active_coords_.insert(active_coords_.end(), values.begin(), values.end());
        active_headers_ = mergedHeaders;
    }

    if (headersChanged) endResetModel();
    if (visible && !headersChanged && newVisibleRows > oldVisibleRows) {
        endInsertRows();
    }
}

void RDBModel::SetActiveCheck(quint64 checkIndex) {
    beginResetModel();
    active_check_index_ = checkIndex;
    has_active_check_ = true;
    active_coords_ = coords_map_.value(checkIndex);
    active_headers_ = header_map_.value(checkIndex);
    endResetModel();
}

void RDBModel::SetActiveCoords(
    const RDB_ALL_DATA_LIST& values,
    const QStringList& headers) {
    beginResetModel();
    has_active_check_ = false;
    active_coords_ = values;
    active_headers_ = headers;
    endResetModel();
}

RDB_DATA_PTR RDBModel::CheckAt(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < chip_list_.size()
        ? chip_list_[static_cast<std::size_t>(row)] : RDB_DATA_PTR();
}

RDB_ALL_DATA_PTR RDBModel::CoordAt(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < active_coords_.size()
        ? active_coords_[static_cast<std::size_t>(row)] : RDB_ALL_DATA_PTR();
}

const RDB_DATA_LIST& RDBModel::ChipList() const {
    return chip_list_;
}

const RDB_ALL_DATA_LIST& RDBModel::CoordList() const {
    return coords_list_;
}

RDB_ALL_DATA_LIST RDBModel::CoordsForCheck(quint64 checkIndex) const {
    return coords_map_.value(checkIndex);
}

QStringList RDBModel::HeadersForCheck(quint64 checkIndex) const {
    return header_map_.value(checkIndex);
}

QStringList RDBModel::ActiveHeaders() const {
    return active_headers_;
}

double RDBModel::DBU() const {
    return dbu_;
}

QString RDBModel::TopCell() const {
    return topcell_;
}

QString RDBModel::CoordinateText(const RDB_ALL_DATA& value) const {
    QStringList fields;
    fields.reserve(static_cast<int>(value.vertex_list.size()));
    for (std::size_t i = 0; i < value.vertex_list.size(); ++i) {
        fields.append(QString::number(static_cast<qlonglong>(value.vertex_list[i])));
    }
    return fields.join(QLatin1Char(' '));
}

void RDBModel::MergeHeaders(
    QStringList& destination,
    const QStringList& source) {
    for (int i = 0; i < source.size(); ++i) {
        if (!source[i].isEmpty() && !destination.contains(source[i])) {
            destination.append(source[i]);
        }
    }
}
