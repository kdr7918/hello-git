#ifndef QT_RDB_VIEWER_MODELS_HPP
#define QT_RDB_VIEWER_MODELS_HPP

#include "rdb_check_index.hpp"

#include <QAbstractTableModel>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTableView>
#include <QVector>

enum class RdbViewerMode { CoordinatesOnly, AllParameters };

/*
 * Result 하나를 표시하기 위한 UI 전용 레코드다. 파서의 대용량 자료구조를 복제하지
 * 않고, 화면에 필요한 문자열과 안정 선택 키만 보관한다.
 */
struct DetailRow {
    QString key;
    QString resultLabel;
    QString coordinates;
    QMap<QString, QStringList> taggedValues;

    DetailRow();
    QString stableKey() const;
};

class CheckTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name = 0, ResultCount, ColumnCount };

    explicit CheckTableModel(QObject* parent = 0);
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

class ResultTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ResultTableModel(QObject* parent = 0);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setMode(RdbViewerMode mode);
    RdbViewerMode mode() const;
    void clear();
    void appendRows(const QVector<DetailRow>& rows);
    void replaceRows(const QVector<DetailRow>& rows);
    const DetailRow& rowAt(int row) const;
    int findRowByStableKey(const QString& key) const;

private:
    void rebuildTagKeys();
    void validateRowCapacity(qsizetype current, qsizetype additional) const;

    RdbViewerMode mode_;
    QVector<DetailRow> rows_;
    QStringList tagKeys_;
};

struct TableSelectionSnapshot {
    QStringList selectedKeys;
    QString currentKey;
    bool hasCurrent;
    int verticalScroll;

    TableSelectionSnapshot() : hasCurrent(false), verticalScroll(0) {}
};

class TableSelectionKeeper {
public:
    static TableSelectionSnapshot capture(const QTableView& view,
                                          const ResultTableModel& model);
    static void restore(QTableView& view,
                        const ResultTableModel& model,
                        const TableSelectionSnapshot& snapshot);
};

#endif // QT_RDB_VIEWER_MODELS_HPP
