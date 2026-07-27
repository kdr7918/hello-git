#ifndef QT_RDB_VIEWER_MODELS_HPP
#define QT_RDB_VIEWER_MODELS_HPP

#include "rdb_check_index.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <QTableView>
#include <QVector>

struct DetailRow {
    enum KindValue { Polygon = 0, EdgeCluster = 1 };

    KindValue kind;
    quint32 ordinal;
    quint32 geometryCount;
    QString summary;

    DetailRow();
    static DetailRow polygon(quint32 ordinal, quint32 geometryCount, const QString& summary);
    static DetailRow edgeCluster(quint32 ordinal, quint32 geometryCount, const QString& summary);
    quint64 stableKey() const;
};

class CheckTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name = 0, ResultCount, Offset, Comment, ColumnCount };

    explicit CheckTableModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setIndex(const rdb::CheckIndexDatabase& index);
    const rdb::CheckIndexEntry& entryAt(int row) const;
    const rdb::CheckIndexDatabase& indexDatabase() const;

private:
    rdb::CheckIndexDatabase index_;
};

class DetailTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Kind = 0, Ordinal, GeometryCount, Summary, ColumnCount };

    explicit DetailTableModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void clear();
    void appendRows(const QVector<DetailRow>& rows);
    void replaceRows(const QVector<DetailRow>& rows);
    static void validateRowCapacity(qsizetype current, qsizetype additional);
    const DetailRow& rowAt(int row) const;
    int findRowByStableKey(quint64 key) const;

private:
    QVector<DetailRow> rows_;
};

struct TableSelectionSnapshot {
    QVector<quint64> selectedKeys;
    quint64 currentKey;
    bool hasCurrent;
    int verticalScroll;

    TableSelectionSnapshot() : currentKey(0), hasCurrent(false), verticalScroll(0) {}
};

class TableSelectionKeeper {
public:
    static TableSelectionSnapshot capture(const QTableView& view,
                                          const DetailTableModel& model);
    static void restore(QTableView& view,
                        const DetailTableModel& model,
                        const TableSelectionSnapshot& snapshot);
};

#endif // QT_RDB_VIEWER_MODELS_HPP
