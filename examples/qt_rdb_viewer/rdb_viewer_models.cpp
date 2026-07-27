#include "rdb_viewer_models.hpp"

#include <QItemSelectionModel>
#include <QScrollBar>

#include <limits>
#include <stdexcept>

DetailRow::DetailRow() {}

QString DetailRow::stableKey() const { return key; }

CheckTableModel::CheckTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int CheckTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(index_.checks.size());
}

int CheckTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CheckTableModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= rowCount()) {
        return QVariant();
    }
    const rdb::CheckIndexEntry& entry = index_.checks[static_cast<std::size_t>(modelIndex.row())];
    if (role == Qt::DisplayRole) {
        if (modelIndex.column() == Name) return QString::fromStdString(entry.name);
        if (modelIndex.column() == ResultCount) return entry.geometry_count;
    }
    if (role == Qt::ToolTipRole && modelIndex.column() == Name) {
        return QString::fromStdString(entry.comment);
    }
    return QVariant();
}

QVariant CheckTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    if (section == Name) return tr("Check Name");
    if (section == ResultCount) return tr("Count");
    return QVariant();
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

ResultTableModel::ResultTableModel(QObject* parent)
    : QAbstractTableModel(parent), mode_(RdbViewerMode::CoordinatesOnly) {}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int ResultTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return mode_ == RdbViewerMode::CoordinatesOnly ? 1 : 1 + tagKeys_.size();
}

QVariant ResultTableModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= rows_.size()) {
        return QVariant();
    }
    const DetailRow& row = rows_[modelIndex.row()];
    QString text;
    if (mode_ == RdbViewerMode::CoordinatesOnly) {
        text = row.coordinates;
    } else if (modelIndex.column() == 0) {
        text = row.resultLabel;
    } else {
        const QString key = tagKeys_.at(modelIndex.column() - 1);
        text = row.taggedValues.value(key).join(QStringLiteral("\n"));
    }
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return text;
    if (role == Qt::TextAlignmentRole && mode_ == RdbViewerMode::CoordinatesOnly) {
        return QVariant::fromValue<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    if (mode_ == RdbViewerMode::CoordinatesOnly) return section == 0 ? tr("Coords") : QVariant();
    if (section == 0) return tr("Result");
    return section - 1 < tagKeys_.size() ? tagKeys_.at(section - 1) : QVariant();
}

void ResultTableModel::setMode(RdbViewerMode mode) {
    if (mode_ == mode) return;
    beginResetModel();
    mode_ = mode;
    rows_.clear();
    tagKeys_.clear();
    endResetModel();
}

RdbViewerMode ResultTableModel::mode() const { return mode_; }

void ResultTableModel::clear() {
    if (rows_.isEmpty() && tagKeys_.isEmpty()) return;
    beginResetModel();
    rows_.clear();
    tagKeys_.clear();
    endResetModel();
}

void ResultTableModel::appendRows(const QVector<DetailRow>& rows) {
    if (rows.isEmpty()) return;
    validateRowCapacity(rows_.size(), rows.size());
    QStringList nextTagKeys = tagKeys_;
    if (mode_ == RdbViewerMode::AllParameters) {
        for (const DetailRow& row : rows) {
            for (QMap<QString, QStringList>::const_iterator it = row.taggedValues.constBegin();
                 it != row.taggedValues.constEnd(); ++it) {
                if (!nextTagKeys.contains(it.key())) nextTagKeys.append(it.key());
            }
        }
        nextTagKeys.sort();
    }
    if (nextTagKeys != tagKeys_) {
        beginResetModel();
        tagKeys_ = nextTagKeys;
        rows_ += rows;
        endResetModel();
        return;
    }
    const int first = rows_.size();
    beginInsertRows(QModelIndex(), first, first + rows.size() - 1);
    rows_ += rows;
    endInsertRows();
}

void ResultTableModel::replaceRows(const QVector<DetailRow>& rows) {
    validateRowCapacity(0, rows.size());
    beginResetModel();
    rows_ = rows;
    rebuildTagKeys();
    endResetModel();
}

const DetailRow& ResultTableModel::rowAt(int row) const {
    if (row < 0 || row >= rows_.size()) throw std::out_of_range("ResultTableModel row");
    return rows_[row];
}

int ResultTableModel::findRowByStableKey(const QString& key) const {
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].stableKey() == key) return row;
    }
    return -1;
}

void ResultTableModel::rebuildTagKeys() {
    tagKeys_.clear();
    if (mode_ != RdbViewerMode::AllParameters) return;
    for (const DetailRow& row : rows_) {
        for (QMap<QString, QStringList>::const_iterator it = row.taggedValues.constBegin();
             it != row.taggedValues.constEnd(); ++it) {
            if (!tagKeys_.contains(it.key())) tagKeys_.append(it.key());
        }
    }
    tagKeys_.sort();
}

void ResultTableModel::validateRowCapacity(qsizetype current, qsizetype additional) const {
    const qsizetype maximum = static_cast<qsizetype>(std::numeric_limits<int>::max());
    if (current < 0 || additional < 0 || current > maximum || additional > maximum - current) {
        throw std::length_error("RDB detail rows exceed Qt model row capacity");
    }
}

TableSelectionSnapshot TableSelectionKeeper::capture(
    const QTableView& view, const ResultTableModel& model) {
    TableSelectionSnapshot snapshot;
    if (view.verticalScrollBar()) snapshot.verticalScroll = view.verticalScrollBar()->value();
    if (!view.selectionModel()) return snapshot;
    const QModelIndexList selected = view.selectionModel()->selectedRows();
    for (const QModelIndex& index : selected) {
        if (index.row() >= 0 && index.row() < model.rowCount()) {
            snapshot.selectedKeys.append(model.rowAt(index.row()).stableKey());
        }
    }
    const QModelIndex current = view.currentIndex();
    if (current.isValid() && current.row() >= 0 && current.row() < model.rowCount()) {
        snapshot.currentKey = model.rowAt(current.row()).stableKey();
        snapshot.hasCurrent = true;
    }
    return snapshot;
}

void TableSelectionKeeper::restore(
    QTableView& view, const ResultTableModel& model, const TableSelectionSnapshot& snapshot) {
    QItemSelectionModel* selection = view.selectionModel();
    if (!selection) return;
    selection->clearSelection();
    for (const QString& key : snapshot.selectedKeys) {
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
