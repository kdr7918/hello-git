#ifndef RDB_CHECK_TREE_MODEL_HPP
#define RDB_CHECK_TREE_MODEL_HPP

#include "ascii_rdb.hpp"
#include "rdb_check_index.hpp"

#include <QHash>
#include <QPair>
#include <QStandardItemModel>
#include <QStringList>
#include <QVector>

#include <vector>

struct GroupingDimension {
    enum Kind { CheckName, TaggedValueKey };

    Kind kind;
    QString key;

    GroupingDimension() : kind(CheckName) {}
    explicit GroupingDimension(Kind type, const QString& value = QString())
        : kind(type), key(value) {}
    bool equals(const GroupingDimension& other) const {
        return kind == other.kind && key == other.key;
    }
};

struct GroupCondition {
    GroupingDimension dimension;
    QString value;
};

struct TreeSelection {
    QVector<int> checkRows;
    std::vector<GroupCondition> conditions;
    QString identity;
};

/*
 * Check 목록과 All Params 그룹 Tree를 전담하는 모델이다. View는 이 모델을 표시만
 * 하고, 선택된 노드가 뜻하는 Check/Result 조건은 selectionForIndex()로 얻는다.
 */
class CheckTreeModel : public QStandardItemModel {
public:
    explicit CheckTreeModel(QObject* parent = 0);

    void clearTree();
    void rebuildIndex(const rdb::CheckIndexDatabase& index);
    void rebuildFull(const rdb::Database& database);
    bool hasFullDatabase() const;

    void resetGrouping();
    void setGrouping(int depth, const GroupingDimension& dimension);
    const std::vector<GroupingDimension>& grouping() const;
    QStringList availableTagKeys() const;

    bool selectionForIndex(const QModelIndex& index, TreeSelection& selection) const;
    QString selectionIdentity(const QModelIndex& index) const;
    QModelIndex indexForIdentity(const QString& identity) const;
    bool matchesConditions(int checkRow,
                           const rdb::Result& result,
                           const std::vector<GroupCondition>& conditions) const;

private:
    typedef QVector<QPair<int, rdb::Index> > ResultList;

    void rebuildFullTree();
    void appendFullTreeLevel(QStandardItem* parent,
                             int depth,
                             const ResultList& results,
                             const std::vector<GroupCondition>& conditions);
    void appendTreeRow(QStandardItem* parent,
                       const QString& label,
                       qulonglong count,
                       const TreeSelection& selection);
    QString addSelection(const TreeSelection& selection);
    QStringList valuesForDimension(int checkRow,
                                   const rdb::Result& result,
                                   const GroupingDimension& dimension) const;
    static QString conditionIdentity(const std::vector<GroupCondition>& conditions);
    static QString checkIdentity(const QString& name);
    void setHeaders();

    const rdb::Database* database_;
    std::vector<GroupingDimension> grouping_;
    QHash<QString, TreeSelection> selections_;
    quint64 selectionSerial_;
};

#endif // RDB_CHECK_TREE_MODEL_HPP
