#ifndef RDB_TREE_MODEL_HPP
#define RDB_TREE_MODEL_HPP

#include "ascii_rdb.hpp"

#include <QAbstractItemModel>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

// All Params 화면에서 Check와 Tagged Value를 계층으로 묶어 표시한다.
class RdbTreeModel : public QAbstractItemModel {
public:
    explicit RdbTreeModel(QObject* parent = 0);
    ~RdbTreeModel() override;

    // QAbstractItemModel 오버라이드는 Qt 시그니처를 그대로 사용한다.
    QModelIndex index(
        int row,
        int column,
        const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void SetDatabase(const std::shared_ptr<rdb::Database>& database);
    std::shared_ptr<rdb::Database> GetDatabase() const;

    void SetGroupingCategories(const QStringList& categories);
    QStringList GetGroupingCategories() const;
    QStringList GetAvailableCategories() const;
    void Rebuild();

    std::vector<rdb::CheckId> GetCheckIds(const QModelIndex& index) const;
    std::vector<rdb::Index> GetResultIndices(const QModelIndex& index) const;
    bool HasExactResultSelection(const QModelIndex& index) const;

    static QString CheckNameCategory();

private:
    struct Node;

    Node* NodeFromIndex(const QModelIndex& index) const;
    QString CategoryValue(
        const QString& category,
        rdb::CheckId checkId,
        const rdb::Result* result) const;
    void BuildCheckNameTree(bool allLoaded);
    void BuildGroupedTree();

    std::shared_ptr<rdb::Database> database_;
    std::unique_ptr<Node> root_;
    QStringList grouping_categories_;
};

#endif // RDB_TREE_MODEL_HPP
