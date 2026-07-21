#include "detail_table_model.h"

DetailTableModel::DetailTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DetailTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int DetailTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 5;
}

QVariant DetailTableModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid() || modelIndex.row() < 0 ||
        modelIndex.row() >= m_rows.size()) {
        return QVariant();
    }

    const DataRow &row = m_rows.at(modelIndex.row());
    if (role == Qt::DisplayRole) {
        switch (modelIndex.column()) {
        case 0: return row.lineNumber;
        case 1: return row.sourceOffset;
        case 2: return row.key;
        case 3: return row.value;
        case 4: return row.raw;
        default: return QVariant();
        }
    }
    if (role == Qt::UserRole)
        return row.sourceOffset;
    if (role == Qt::TextAlignmentRole && modelIndex.column() < 2)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return QVariant();
}

QVariant DetailTableModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Vertical)
        return section + 1;

    switch (section) {
    case 0: return tr("Line");
    case 1: return tr("File offset");
    case 2: return tr("Key");
    case 3: return tr("Value");
    case 4: return tr("Raw record");
    default: return QVariant();
    }
}

Qt::ItemFlags DetailTableModel::flags(const QModelIndex &modelIndex) const
{
    if (!modelIndex.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void DetailTableModel::replaceRows(const QVector<DataRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    rebuildIndex();
    endResetModel();
}

void DetailTableModel::appendRows(const QVector<DataRow> &rows)
{
    if (rows.isEmpty())
        return;

    const int first = m_rows.size();
    const int last = first + rows.size() - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_rows.reserve(m_rows.size() + rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        m_rows.append(rows.at(i));
        m_rowByStableKey.insert(rows.at(i).sourceOffset, first + i);
    }
    endInsertRows();
}

quint64 DetailTableModel::stableKeyAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return 0;
    return m_rows.at(row).sourceOffset;
}

int DetailTableModel::rowForStableKey(quint64 key) const
{
    return m_rowByStableKey.value(key, -1);
}

void DetailTableModel::rebuildIndex()
{
    m_rowByStableKey.clear();
    m_rowByStableKey.reserve(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i)
        m_rowByStableKey.insert(m_rows.at(i).sourceOffset, i);
}
