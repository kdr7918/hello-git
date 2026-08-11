#ifndef DATABASE_APP_RDB_TREE_MODEL_HPP
#define DATABASE_APP_RDB_TREE_MODEL_HPP

#include "rdb_database_support.hpp"

#include <QAbstractItemModel>
#include <QMap>
#include <QStack>
#include <QStringList>

#include <vector>

class RDBTreeItem {
public:
    explicit RDBTreeItem(
        const QVariant& title = QVariant(),
        RDBTreeItem* parent = 0);
    ~RDBTreeItem();

    RDBTreeItem* FindOrCreateChild(const QVariant& title);
    RDBTreeItem* Child(int row) const;
    int ChildCount() const;
    int Row() const;
    RDBTreeItem* Parent() const;
    void AddResult(rdb::CheckId checkId, rdb::Index resultIndex);
    void SortChildren();

    int depth;
    quint64 count;
    quint64 chip_index;
    std::vector<RDBTreeItem*> m_childItems;
    std::vector<rdb::CheckId> check_ids;
    std::vector<rdb::Index> result_indices;
    QVariant title;
    RDBTreeItem* m_parentItem;
    bool exact_results;
    bool implicit_check_results;
};

class RDBTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum CHECK_TREE_COL {
        KEY = 0,
        VALUE,
        CHECK_TREE_COL_COUNT
    };

    explicit RDBTreeModel(QObject* parent = 0);
    ~RDBTreeModel() override;

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

    void InitGetterFunc();
    void SetDatabase(const RDB_DATABASE_PTR& database);
    RDB_DATABASE_PTR GetDatabase() const;
    void SetCompKeys(const QStringList& keys);
    void SortForGrouping();
    void BuildRDBTree();
    void BuildRDBCheckTree();
    void SetDBU(double dbu);
    std::vector<rdb::CheckId> GetCheckIds(const QModelIndex& index) const;
    std::vector<rdb::Index> GetResultIndices(const QModelIndex& index) const;
    bool HasExactResultSelection(const QModelIndex& index) const;
    QStringList GetCompKey() const;
    QStringList GetAvailableCategories() const;
    quint64 GetChipIndex(const QModelIndex& index, bool* ok = 0) const;
    void SearchListBFS(const QString& text);
    QModelIndex SearchNext(int direction);
    QModelIndex GetSearchIndex() const;
    int SearchCount() const;
    int SearchPosition() const;

public slots:
    void InitSearch(const QString& text);

private:
    RDBTreeItem* ItemFromIndex(const QModelIndex& index) const;
    QVariant GroupValue(
        const QString& key,
        rdb::CheckId checkId,
        rdb::Index resultIndex) const;
    void CollectSearchIndexes(
        const QModelIndex& parent,
        const QString& text);

    double dbu_;
    int cur_index_;
    QStack<RDBTreeItem*> dfs_stack_;
    std::vector<QModelIndex> search_list_;
    std::vector<QModelIndex> total_list_;
    RDBTreeItem* root_item_;
    RDB_DATABASE_PTR database_;
    std::vector<QVariant> comp_keys_;
    QMap<QString, std::vector<rdb::StringId> > group_tag_ids_;
    mutable QStringList available_categories_cache_;
    mutable bool available_categories_cache_valid_;
    mutable std::size_t available_categories_tag_count_;
};

#endif // DATABASE_APP_RDB_TREE_MODEL_HPP
