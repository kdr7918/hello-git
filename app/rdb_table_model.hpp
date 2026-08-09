#ifndef RDB_TABLE_MODEL_HPP
#define RDB_TABLE_MODEL_HPP

#include "rdb_check_detail.hpp"

#include <QAbstractTableModel>
#include <QString>

#include <cstddef>
#include <memory>
#include <vector>

class RdbTableModel : public QAbstractTableModel {
public:
    enum ModelType {
        CheckIndex,
        AllParameters,
        CoordinatesOnly
    };

    enum CheckColumn {
        CheckNameColumn = 0,
        ResultCountColumn,
        CheckColumnCount
    };

    explicit RdbTableModel(ModelType type, QObject* parent = 0);

    // QAbstractTableModel 오버라이드는 Qt 시그니처를 그대로 사용한다.
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    ModelType GetModelType() const;

    void SetCheckIndexDatabase(rdb::CheckIndexDatabase index);
    void SetDatabase(
        ModelType type,
        const std::shared_ptr<rdb::Database>& database);
    std::shared_ptr<rdb::Database> GetDatabase() const;

    rdb::CheckId CheckIdAt(int modelRow) const;
    void SelectDetailCheck(rdb::CheckId checkId);
    void SelectDetailChecks(const std::vector<rdb::CheckId>& checkIds);
    void SelectDetailResults(
        const std::vector<rdb::CheckId>& checkIds,
        const std::vector<rdb::Index>& resultIndices);
    rdb::CheckId SelectedCheckId() const;
    const std::vector<rdb::CheckId>& GetSelectedCheckIds() const;
    bool IsCheckSelected(rdb::CheckId checkId) const;
    bool IsSelectedDetailLoaded() const;
    bool IsDetailLoadActive() const;
    rdb::CheckId LoadingCheckId() const;

    void BeginDetailLoad(rdb::CheckId checkId);
    void AppendDetailResults(std::vector<rdb::DetailResult> results);
    void FinishDetailLoad(const rdb::CheckDetail& detail);
    void CancelDetailLoad();

    std::size_t TotalRowCount() const;
    std::size_t RowOffset() const;
    void SetRowOffset(std::size_t offset);

private:
    // STL 기반 Database에서 현재 Qt View에 노출할 구간만 계산한다.
    int VisibleRowCount(std::size_t totalRows) const;
    std::size_t SourceRow(int modelRow) const;
    const rdb::RuleCheck* SelectedCheck() const;
    const rdb::Result* DetailResultAt(int modelRow) const;
    void RebuildTagColumns();
    rdb::StringId InternTagName(const std::string& name);
    QVariant CheckIndexData(const QModelIndex& index, int role) const;
    QVariant DetailData(const QModelIndex& index, int role) const;

    ModelType type_;
    std::shared_ptr<rdb::Database> database_;
    std::vector<rdb::CheckId> selected_check_ids_;
    std::vector<rdb::Index> selected_result_indices_;
    bool exact_result_selection_;
    std::vector<rdb::StringId> tagged_value_columns_;
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

#endif // RDB_TABLE_MODEL_HPP
