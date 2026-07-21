#ifndef DETAIL_TABLE_MODEL_H
#define DETAIL_TABLE_MODEL_H

#include "data_types.h"

#include <QAbstractTableModel>
#include <QHash>

class DetailTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit DetailTableModel(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void replaceRows(const QVector<DataRow> &rows);
    void appendRows(const QVector<DataRow> &rows);
    quint64 stableKeyAt(int row) const;
    int rowForStableKey(quint64 key) const;

private:
    void rebuildIndex();

    QVector<DataRow> m_rows;
    QHash<quint64, int> m_rowByStableKey;
};

#endif // DETAIL_TABLE_MODEL_H
