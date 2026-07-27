#ifndef RDB_CHECK_TREE_MODEL_HPP
#define RDB_CHECK_TREE_MODEL_HPP

#include "ascii_rdb.hpp"
#include "rdb_check_index.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>

#include <memory>
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
 * Check 목록과 All Params 그룹 Tree를 전담하는 경량 모델이다. QStandardItem을
 * 만들지 않고 Node가 Parser 결과를 위한 최소 표시/선택 정보만 보관한다. View는 이
 * 모델을 표시만 하고, 선택된 노드가 뜻하는 Check/Result 조건은
 * selectionForIndex()로 얻는다.
 */
class CheckTreeModel : public QAbstractItemModel {
public:
    explicit CheckTreeModel(QObject* parent = 0);

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

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

    struct Node {
        explicit Node(Node* parentNode = 0, int rowInParent = -1)
            : parent(parentNode), row(rowInParent), count(0) {}

        Node* parent;
        int row;
        QString label;
        qulonglong count;
        TreeSelection selection;
        std::vector<std::unique_ptr<Node> > children;
    };

    Node* nodeForIndex(const QModelIndex& index) const;
    void clearNodes();
    void rebuildFullTree();
    void appendFullTreeLevel(Node* parent,
                             int depth,
                             const ResultList& results,
                             const std::vector<GroupCondition>& conditions);
    Node* appendTreeRow(Node* parent,
                        const QString& label,
                        qulonglong count,
                        const TreeSelection& selection);
    QStringList valuesForDimension(int checkRow,
                                   const rdb::Result& result,
                                   const GroupingDimension& dimension) const;
    static QString conditionIdentity(const std::vector<GroupCondition>& conditions);
    static QString checkIdentity(const QString& name);

    const rdb::Database* database_;
    std::vector<GroupingDimension> grouping_;
    std::unique_ptr<Node> root_;
    QHash<QString, Node*> nodesByIdentity_;
};

#endif // RDB_CHECK_TREE_MODEL_HPP
