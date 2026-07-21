#include "toc_tree_model.h"

#include <QtAlgorithms>

TocTreeModel::Node::~Node()
{
    qDeleteAll(children);
}

TocTreeModel::TocTreeModel(QObject *parent)
    : QAbstractItemModel(parent), m_root(new Node)
{
}

TocTreeModel::~TocTreeModel()
{
    delete m_root;
}

QModelIndex TocTreeModel::index(int row,
                                int column,
                                const QModelIndex &parentIndex) const
{
    if (!hasIndex(row, column, parentIndex))
        return QModelIndex();

    Node *parentNode = nodeForIndex(parentIndex);
    if (!parentNode || row < 0 || row >= parentNode->children.size())
        return QModelIndex();
    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex TocTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    Node *childNode = nodeForIndex(child);
    Node *parentNode = childNode ? childNode->parent : 0;
    if (!parentNode || parentNode == m_root)
        return QModelIndex();

    Node *grandParent = parentNode->parent;
    const int row = grandParent ? grandParent->children.indexOf(parentNode) : 0;
    return createIndex(row, 0, parentNode);
}

int TocTreeModel::rowCount(const QModelIndex &parentIndex) const
{
    if (parentIndex.column() > 0)
        return 0;
    Node *node = nodeForIndex(parentIndex);
    return node ? node->children.size() : 0;
}

int TocTreeModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant TocTreeModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid())
        return QVariant();

    Node *node = nodeForIndex(modelIndex);
    if (!node)
        return QVariant();

    if (role == Qt::DisplayRole) {
        if (modelIndex.column() == 0)
            return node->entry.title;
        if (modelIndex.column() == 1)
            return node->entry.estimatedRows;
    }
    if (role == Qt::UserRole)
        return node->entry.id;
    if (role == Qt::ToolTipRole) {
        return tr("Offset %1, length %2 bytes")
            .arg(node->entry.byteOffset)
            .arg(node->entry.byteLength);
    }
    return QVariant();
}

QVariant TocTreeModel::headerData(int section,
                                  Qt::Orientation orientation,
                                  int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    if (section == 0)
        return tr("Table of contents");
    if (section == 1)
        return tr("Rows");
    return QVariant();
}

Qt::ItemFlags TocTreeModel::flags(const QModelIndex &modelIndex) const
{
    if (!modelIndex.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void TocTreeModel::setEntries(const QVector<TocEntry> &entries)
{
    beginResetModel();
    clear();

    QHash<qint64, Node *> nodesById;
    for (int i = 0; i < entries.size(); ++i) {
        const TocEntry &entry = entries.at(i);
        Node *node = new Node;
        node->entry = entry;
        Node *parentNode = nodesById.value(entry.parentId, m_root);
        node->parent = parentNode;
        parentNode->children.append(node);
        nodesById.insert(entry.id, node);
    }
    endResetModel();
}

TocEntry TocTreeModel::entryForIndex(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node != m_root ? node->entry : TocEntry();
}

QModelIndex TocTreeModel::firstEntryIndex() const
{
    return m_root->children.isEmpty()
        ? QModelIndex()
        : createIndex(0, 0, m_root->children.first());
}

void TocTreeModel::clear()
{
    qDeleteAll(m_root->children);
    m_root->children.clear();
}

TocTreeModel::Node *TocTreeModel::nodeForIndex(const QModelIndex &modelIndex) const
{
    if (!modelIndex.isValid())
        return m_root;
    return static_cast<Node *>(modelIndex.internalPointer());
}
