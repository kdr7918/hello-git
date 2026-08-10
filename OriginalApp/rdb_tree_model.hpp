#ifndef ORIGINAL_APP_RDB_TREE_MODEL_HPP
#define ORIGINAL_APP_RDB_TREE_MODEL_HPP

#include "rdb_types.hpp"

#include <QAbstractItemModel>
#include <QMap>
#include <QSet>
#include <QStack>

#include <functional>
#include <vector>

typedef std::function<QVariant(const RDB_ALL_DATA_PTR&)> RDB_TREE_GETTER;

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
    void BuildTree(
        const RDB_ALL_DATA_PTR& coord,
        const QStringList& compKeys,
        const std::vector<RDB_TREE_GETTER>& getterFunctions,
        int keyIndex = 0);
    void AddCoord(const RDB_ALL_DATA_PTR& coord);
    void AddHeader(const QString& header);

    int depth;
    quint64 count;
    quint64 chip_index;
    std::vector<RDBTreeItem*> m_childItems;
    QSet<QString> headers;
    QMap<quint64, RDB_ALL_DATA_PTR> coords_map;
    QVariant title;
    RDBTreeItem* m_parentItem;
};

class RDBTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum CHECK_TREE_COL {
        KEY = 0,
        VALUE,
        CHECK_TREE_COL_COUNT
    };

    typedef RDB_TREE_GETTER GetterFunc;

    explicit RDBTreeModel(QObject* parent = 0);
    RDBTreeModel(const RDB_DATA_LIST& chips, QObject* parent = 0);
    RDBTreeModel(const RDB_ALL_DATA_LIST& coords, QObject* parent = 0);
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

    void InitGetterFunc();
    void SetChecks(const RDB_DATA_LIST& chips);
    void SetCoords(const RDB_ALL_DATA_LIST& coords);
    void SetCompKeys(const QStringList& keys);
    void SortForGrouping();
    void BuildRDBTree();
    void BuildRDBCheckTree();
    void SetDBU(double dbu);
    RDB_ALL_DATA_LIST GetCoordsList(const QModelIndex& index) const;
    QStringList GetHeaderList(const QModelIndex& index) const;
    QStringList GetCompKey() const;
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
        const RDB_ALL_DATA_PTR& coord) const;
    void CollectSearchIndexes(
        const QModelIndex& parent,
        const QString& text);

    double dbu_;
    int cur_index_;
    QStack<RDBTreeItem*> dfs_stack_;
    std::vector<QModelIndex> search_list_;
    std::vector<QModelIndex> total_list_;
    RDBTreeItem* root_item_;
    RDB_ALL_DATA_LIST coords_;
    RDB_DATA_LIST chips_;
    std::vector<QVariant> comp_keys_;
    QMap<QString, GetterFunc> getter_func_;
};

#endif // ORIGINAL_APP_RDB_TREE_MODEL_HPP
