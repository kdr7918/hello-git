#ifndef ORIGINAL_APP_RDB_MODEL_HPP
#define ORIGINAL_APP_RDB_MODEL_HPP

#include "rdb_types.hpp"

#include <QAbstractTableModel>
#include <QMap>

class RDBModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum MODEL_TYPE {
        CHIP_TABLE = 0,
        COORDS_ONLY = 1,
        ALL_PARAMS = 2
    };

    enum CHECK_TABLE_COL {
        ID = 0,
        NAME,
        COUNT,
        SEEK,
        CHECK_TABLE_COL_COUNT
    };

    explicit RDBModel(MODEL_TYPE type, QObject* parent = 0);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    void Clear();
    void SetType(MODEL_TYPE type);
    MODEL_TYPE GetType() const;
    void SetIndexResult(const RDB_INDEX_RESULT_PTR& result);
    void AppendCoords(
        quint64 checkIndex,
        const RDB_ALL_DATA_LIST& values,
        const QStringList& headers);
    void SetActiveCheck(quint64 checkIndex);
    void SetActiveCoords(
        const RDB_ALL_DATA_LIST& values,
        const QStringList& headers);

    RDB_DATA_PTR CheckAt(int row) const;
    RDB_ALL_DATA_PTR CoordAt(int row) const;
    const RDB_DATA_LIST& ChipList() const;
    const RDB_ALL_DATA_LIST& CoordList() const;
    RDB_ALL_DATA_LIST CoordsForCheck(quint64 checkIndex) const;
    QStringList HeadersForCheck(quint64 checkIndex) const;
    QStringList ActiveHeaders() const;
    double DBU() const;
    QString TopCell() const;

private:
    QString CoordinateText(const RDB_ALL_DATA& value) const;
    void MergeHeaders(QStringList& destination, const QStringList& source);

    MODEL_TYPE type_;
    double dbu_;
    QString topcell_;
    RDB_DATA_LIST chip_list_;
    RDB_ALL_DATA_LIST coords_list_;
    QMap<quint64, RDB_ALL_DATA_LIST> coords_map_;
    QMap<quint64, QStringList> header_map_;
    RDB_ALL_DATA_LIST active_coords_;
    QStringList active_headers_;
    quint64 active_check_index_;
    bool has_active_check_;
};

#endif // ORIGINAL_APP_RDB_MODEL_HPP
