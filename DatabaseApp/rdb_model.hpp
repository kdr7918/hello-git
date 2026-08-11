#ifndef DATABASE_APP_RDB_MODEL_HPP
#define DATABASE_APP_RDB_MODEL_HPP

#include "rdb_database_support.hpp"

#include <QAbstractTableModel>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

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
    void SetDatabase(const RDB_DATABASE_PTR& database);
    RDB_DATABASE_PTR GetDatabase() const;

    void BeginCheckLoad(rdb::CheckId checkId);
    void AppendCoords(
        rdb::CheckId checkId,
        const RDB_DETAIL_BATCH_PTR& batch);
    void FinishCheckLoad(
        rdb::CheckId checkId,
        const rdb::CheckDetail& detail);
    void CancelCheckLoad();

    void SetActiveCheck(quint64 checkIndex);
    void SetActiveChecks(const std::vector<rdb::CheckId>& checkIds);
    void SetActiveResults(
        const std::vector<rdb::CheckId>& checkIds,
        const std::vector<rdb::Index>& resultIndices);
    rdb::CheckId CheckIdAt(int row) const;
    rdb::Index ResultIndexAt(int row) const;
    QStringList AvailableHeaders() const;
    double DBU() const;
    QString TopCell() const;

    std::size_t TotalRowCount() const;
    std::size_t RowOffset() const;
    void SetRowOffset(std::size_t offset);

private:
    int VisibleRowCount(std::size_t totalRows) const;
    std::size_t SourceRow(int modelRow) const;
    rdb::Index ResultIndexForSource(std::size_t source) const;
    const rdb::Result* ResultAt(int modelRow) const;
    void RebuildActiveHeaders();
    rdb::StringId InternTagName(const std::string& name);
    QVariant CheckIndexData(const QModelIndex& index, int role) const;
    QVariant DetailData(const QModelIndex& index, int role) const;
    QString CoordinateText(const rdb::Result& value) const;

    MODEL_TYPE type_;
    RDB_DATABASE_PTR database_;
    std::vector<rdb::CheckId> active_check_ids_;
    std::vector<rdb::Index> active_result_indices_;
    bool exact_result_selection_;
    std::vector<rdb::StringId> active_headers_;
    std::vector<rdb::StringId> interned_tag_names_;
    std::size_t row_offset_;

    bool detail_load_active_;
    rdb::CheckId loading_check_id_;
    std::unique_ptr<rdb::StringTable::Checkpoint> string_checkpoint_;
    rdb::RuleCheck original_check_;
    std::size_t result_checkpoint_;
    std::size_t vertex_checkpoint_;
    std::size_t edge_checkpoint_;
    std::size_t tagged_value_checkpoint_;
    std::size_t check_text_checkpoint_;
    std::size_t loaded_check_checkpoint_;
    std::size_t interned_tag_checkpoint_;
};

#endif // DATABASE_APP_RDB_MODEL_HPP
