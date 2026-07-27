#include "rdb_viewer_models.hpp"

#include <QItemSelectionModel>
#include <QScrollBar>

#include <limits>
#include <stdexcept>

DetailRow::DetailRow()
    : kind(Polygon), ordinal(0), geometryCount(0) {}

DetailRow DetailRow::polygon(quint32 ordinalValue, quint32 count, const QString& text) {
    DetailRow row;
    row.kind = Polygon;
    row.ordinal = ordinalValue;
    row.geometryCount = count;
    row.summary = text;
    return row;
}

DetailRow DetailRow::edgeCluster(quint32 ordinalValue, quint32 count, const QString& text) {
    DetailRow row;
    row.kind = EdgeCluster;
    row.ordinal = ordinalValue;
    row.geometryCount = count;
    row.summary = text;
    return row;
}

quint64 DetailRow::stableKey() const {
    return (static_cast<quint64>(ordinal) << 1U) |
           static_cast<quint64>(kind == EdgeCluster ? 1U : 0U);
}

CheckTableModel::CheckTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int CheckTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(index_.checks.size());
}

int CheckTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CheckTableModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || role != Qt::DisplayRole ||
        modelIndex.row() < 0 || modelIndex.row() >= rowCount()) return QVariant();
    const rdb::CheckIndexEntry& entry = index_.checks[static_cast<std::size_t>(modelIndex.row())];
    switch (modelIndex.column()) {
    case Name: return QString::fromStdString(entry.name);
    case ResultCount: return entry.geometry_count;
    case Offset: return QVariant::fromValue<qulonglong>(entry.offset);
    case Comment: return QString::fromStdString(entry.comment);
    default: return QVariant();
    }
}

QVariant CheckTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case Name: return tr("Check");
    case ResultCount: return tr("Results");
    case Offset: return tr("Byte offset");
    case Comment: return tr("Comment");
    default: return QVariant();
    }
}

void CheckTableModel::setIndex(const rdb::CheckIndexDatabase& index) {
    if (index.checks.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("RDB check count exceeds Qt model row capacity");
    }
    beginResetModel();
    index_ = index;
    endResetModel();
}

const rdb::CheckIndexEntry& CheckTableModel::entryAt(int row) const {
    if (row < 0 || row >= rowCount()) throw std::out_of_range("CheckTableModel row");
    return index_.checks[static_cast<std::size_t>(row)];
}

const rdb::CheckIndexDatabase& CheckTableModel::indexDatabase() const { return index_; }

DetailTableModel::DetailTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int DetailTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int DetailTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DetailTableModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || role != Qt::DisplayRole ||
        modelIndex.row() < 0 || modelIndex.row() >= rows_.size()) return QVariant();
    const DetailRow& row = rows_[modelIndex.row()];
    switch (modelIndex.column()) {
    case Kind: return row.kind == DetailRow::Polygon ? tr("Polygon") : tr("Edge cluster");
    case Ordinal: return row.ordinal;
    case GeometryCount: return row.geometryCount;
    case Summary: return row.summary;
    default: return QVariant();
    }
}

QVariant DetailTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case Kind: return tr("Kind");
    case Ordinal: return tr("Ordinal");
    case GeometryCount: return tr("Geometry count");
    case Summary: return tr("Detail");
    default: return QVariant();
    }
}

void DetailTableModel::clear() {
    if (rows_.isEmpty()) return;
    beginResetModel();
    rows_.clear();
    endResetModel();
}

void DetailTableModel::appendRows(const QVector<DetailRow>& rows) {
    if (rows.isEmpty()) return;
    validateRowCapacity(rows_.size(), rows.size());
    const int first = static_cast<int>(rows_.size());
    const int last = first + static_cast<int>(rows.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    rows_ += rows;
    endInsertRows();
}

void DetailTableModel::replaceRows(const QVector<DetailRow>& rows) {
    validateRowCapacity(0, rows.size());
    beginResetModel();
    rows_ = rows;
    endResetModel();
}

void DetailTableModel::validateRowCapacity(qsizetype current, qsizetype additional) {
    const qsizetype maximum = static_cast<qsizetype>(std::numeric_limits<int>::max());
    if (current < 0 || additional < 0 || current > maximum || additional > maximum - current) {
        throw std::length_error("RDB detail rows exceed Qt model row capacity");
    }
}

const DetailRow& DetailTableModel::rowAt(int row) const {
    if (row < 0 || row >= rows_.size()) throw std::out_of_range("DetailTableModel row");
    return rows_[row];
}

int DetailTableModel::findRowByStableKey(quint64 key) const {
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].stableKey() == key) return row;
    }
    return -1;
}

TableSelectionSnapshot TableSelectionKeeper::capture(
    const QTableView& view, const DetailTableModel& model) {
    TableSelectionSnapshot snapshot;
    if (view.verticalScrollBar()) snapshot.verticalScroll = view.verticalScrollBar()->value();
    if (!view.selectionModel()) return snapshot;
    const QModelIndexList selected = view.selectionModel()->selectedRows();
    for (const QModelIndex& index : selected) {
        if (index.row() >= 0 && index.row() < model.rowCount()) {
            snapshot.selectedKeys.push_back(model.rowAt(index.row()).stableKey());
        }
    }
    const QModelIndex current = view.currentIndex();
    if (current.isValid() && current.row() < model.rowCount()) {
        snapshot.currentKey = model.rowAt(current.row()).stableKey();
        snapshot.hasCurrent = true;
    }
    return snapshot;
}

void TableSelectionKeeper::restore(
    QTableView& view,
    const DetailTableModel& model,
    const TableSelectionSnapshot& snapshot) {
    QItemSelectionModel* selection = view.selectionModel();
    if (!selection) return;
    selection->clearSelection();
    for (quint64 key : snapshot.selectedKeys) {
        const int row = model.findRowByStableKey(key);
        if (row >= 0) {
            selection->select(model.index(row, 0),
                              QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    if (snapshot.hasCurrent) {
        const int row = model.findRowByStableKey(snapshot.currentKey);
        if (row >= 0) selection->setCurrentIndex(model.index(row, 0), QItemSelectionModel::NoUpdate);
    }
    if (view.verticalScrollBar()) view.verticalScrollBar()->setValue(snapshot.verticalScroll);
}
