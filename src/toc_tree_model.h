#ifndef TOC_TREE_MODEL_H
#define TOC_TREE_MODEL_H

#include "data_types.h"

#include <QAbstractItemModel>
#include <QHash>

class TocTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit TocTreeModel(QObject *parent = 0);
    ~TocTreeModel() override;

    QModelIndex index(int row,
                      int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setEntries(const QVector<TocEntry> &entries);
    TocEntry entryForIndex(const QModelIndex &index) const;
    QModelIndex firstEntryIndex() const;

private:
    struct Node
    {
        Node() : parent(0) {}
        ~Node();

        TocEntry entry;
        Node *parent;
        QVector<Node *> children;
    };

    void clear();
    Node *nodeForIndex(const QModelIndex &index) const;

    Node *m_root;
};

#endif // TOC_TREE_MODEL_H
