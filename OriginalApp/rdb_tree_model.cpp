#include "rdb_tree_model.hpp"

#include <QSet>

#include <algorithm>
#include <limits>

namespace {

QString DisplayKey(const QVariant& value) {
    if (!value.isValid() || value.toString().isEmpty()) {
        return QStringLiteral("<none>");
    }
    return value.toString();
}

} // namespace

RDBTreeItem::RDBTreeItem(const QVariant& itemTitle, RDBTreeItem* parent)
    : depth(parent ? parent->depth + 1 : 0),
      count(0U),
      chip_index(0U),
      title(itemTitle),
      m_parentItem(parent) {}

RDBTreeItem::~RDBTreeItem() {
    for (std::size_t i = 0; i < m_childItems.size(); ++i) {
        delete m_childItems[i];
    }
}

RDBTreeItem* RDBTreeItem::FindOrCreateChild(const QVariant& itemTitle) {
    const QString key = DisplayKey(itemTitle);
    for (std::size_t i = 0; i < m_childItems.size(); ++i) {
        if (DisplayKey(m_childItems[i]->title) == key) return m_childItems[i];
    }
    RDBTreeItem* child = new RDBTreeItem(itemTitle, this);
    m_childItems.push_back(child);
    return child;
}

RDBTreeItem* RDBTreeItem::Child(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < m_childItems.size()
        ? m_childItems[static_cast<std::size_t>(row)] : 0;
}

int RDBTreeItem::ChildCount() const {
    return m_childItems.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(m_childItems.size());
}

int RDBTreeItem::Row() const {
    if (!m_parentItem) return 0;
    for (std::size_t i = 0; i < m_parentItem->m_childItems.size(); ++i) {
        if (m_parentItem->m_childItems[i] == this) return static_cast<int>(i);
    }
    return 0;
}

RDBTreeItem* RDBTreeItem::Parent() const {
    return m_parentItem;
}

void RDBTreeItem::BuildTree(
    const RDB_ALL_DATA_PTR& coord,
    const QStringList& compKeys,
    const std::vector<RDB_TREE_GETTER>& getterFunctions,
    int keyIndex) {
    if (!coord || keyIndex < 0 || keyIndex >= compKeys.size() ||
        static_cast<std::size_t>(keyIndex) >= getterFunctions.size()) {
        return;
    }
    RDBTreeItem* child = FindOrCreateChild(
        getterFunctions[static_cast<std::size_t>(keyIndex)](coord));
    ++child->count;
    child->AddCoord(coord);
    const QStringList coordHeaders = coord->HeaderList();
    for (int i = 0; i < coordHeaders.size(); ++i) {
        child->AddHeader(coordHeaders[i]);
    }
    child->BuildTree(
        coord, compKeys, getterFunctions, keyIndex + 1);
}

void RDBTreeItem::AddCoord(const RDB_ALL_DATA_PTR& coord) {
    if (!coord) return;
    coords_map.insert(coord->index, coord);
    if (coord->check) chip_index = coord->check->index;
}

void RDBTreeItem::AddHeader(const QString& header) {
    if (!header.isEmpty()) headers.insert(header);
}

RDBTreeModel::RDBTreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      dbu_(0.0),
      cur_index_(-1),
      root_item_(new RDBTreeItem) {
    comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    InitGetterFunc();
}

RDBTreeModel::RDBTreeModel(const RDB_DATA_LIST& chips, QObject* parent)
    : QAbstractItemModel(parent),
      dbu_(0.0),
      cur_index_(-1),
      root_item_(new RDBTreeItem),
      chips_(chips) {
    comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    InitGetterFunc();
    BuildRDBCheckTree();
}

RDBTreeModel::RDBTreeModel(const RDB_ALL_DATA_LIST& coords, QObject* parent)
    : QAbstractItemModel(parent),
      dbu_(0.0),
      cur_index_(-1),
      root_item_(new RDBTreeItem),
      coords_(coords) {
    comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    InitGetterFunc();
    BuildRDBTree();
}

RDBTreeModel::~RDBTreeModel() {
    delete root_item_;
}

QModelIndex RDBTreeModel::index(
    int row,
    int column,
    const QModelIndex& parentIndex) const {
    if (row < 0 || column < 0 || column >= CHECK_TREE_COL_COUNT) {
        return QModelIndex();
    }
    RDBTreeItem* parentItem = ItemFromIndex(parentIndex);
    RDBTreeItem* child = parentItem ? parentItem->Child(row) : 0;
    return child ? createIndex(row, column, child) : QModelIndex();
}

QModelIndex RDBTreeModel::parent(const QModelIndex& childIndex) const {
    if (!childIndex.isValid()) return QModelIndex();
    RDBTreeItem* child = ItemFromIndex(childIndex);
    RDBTreeItem* parentItem = child ? child->Parent() : 0;
    if (!parentItem || parentItem == root_item_) return QModelIndex();
    return createIndex(parentItem->Row(), 0, parentItem);
}

int RDBTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.isValid() && parentIndex.column() != 0) return 0;
    RDBTreeItem* item = ItemFromIndex(parentIndex);
    return item ? item->ChildCount() : 0;
}

int RDBTreeModel::columnCount(const QModelIndex&) const {
    return CHECK_TREE_COL_COUNT;
}

QVariant RDBTreeModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid()) return QVariant();
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    if (!item) return QVariant();
    if (role == Qt::DisplayRole) {
        if (modelIndex.column() == KEY) return DisplayKey(item->title);
        if (modelIndex.column() == VALUE) {
            return QVariant::fromValue(item->count);
        }
    }
    if (role == Qt::TextAlignmentRole && modelIndex.column() == VALUE) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant RDBTreeModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    if (section == KEY) return tr("Key");
    if (section == VALUE) return tr("Count");
    return QVariant();
}

void RDBTreeModel::InitGetterFunc() {
    getter_func_.clear();
    getter_func_.insert(
        QStringLiteral("Check Name"),
        [](const RDB_ALL_DATA_PTR& value) -> QVariant {
            return value && value->check ? QVariant(value->check->name) : QVariant();
        });
    getter_func_.insert(
        QStringLiteral("Type"),
        [](const RDB_ALL_DATA_PTR& value) -> QVariant {
            return value
                ? QVariant(QString(QChar::fromLatin1(value->type))) : QVariant();
        });
    getter_func_.insert(
        QStringLiteral("ID"),
        [](const RDB_ALL_DATA_PTR& value) -> QVariant {
            return value ? QVariant::fromValue(value->index) : QVariant();
        });
}

void RDBTreeModel::SetChecks(const RDB_DATA_LIST& chips) {
    chips_ = chips;
    coords_.clear();
    BuildRDBCheckTree();
}

void RDBTreeModel::SetCoords(const RDB_ALL_DATA_LIST& coords) {
    coords_ = coords;
    BuildRDBTree();
}

void RDBTreeModel::SetCompKeys(const QStringList& keys) {
    comp_keys_.clear();
    for (int i = 0; i < keys.size() && i < 3; ++i) {
        if (!keys[i].isEmpty()) comp_keys_.push_back(QVariant(keys[i]));
    }
    if (comp_keys_.empty()) {
        comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    }
    if (coords_.empty()) BuildRDBCheckTree();
    else BuildRDBTree();
}

void RDBTreeModel::SortForGrouping() {
    std::stable_sort(
        coords_.begin(), coords_.end(),
        [this](const RDB_ALL_DATA_PTR& left, const RDB_ALL_DATA_PTR& right) {
            for (std::size_t i = 0; i < comp_keys_.size(); ++i) {
                const QString key = comp_keys_[i].toString();
                const QString leftValue = DisplayKey(GroupValue(key, left));
                const QString rightValue = DisplayKey(GroupValue(key, right));
                const int compared = QString::compare(
                    leftValue, rightValue, Qt::CaseInsensitive);
                if (compared != 0) return compared < 0;
            }
            return left && right ? left->index < right->index
                                 : static_cast<bool>(left);
        });
}

void RDBTreeModel::BuildRDBTree() {
    beginResetModel();
    delete root_item_;
    root_item_ = new RDBTreeItem;
    search_list_.clear();
    total_list_.clear();
    cur_index_ = -1;
    SortForGrouping();

    QStringList keys;
    std::vector<RDB_TREE_GETTER> getters;
    getters.reserve(comp_keys_.size());
    for (std::size_t i = 0; i < comp_keys_.size(); ++i) {
        const QString key = comp_keys_[i].toString();
        keys.append(key);
        getters.push_back(
            [this, key](const RDB_ALL_DATA_PTR& coord) -> QVariant {
                return GroupValue(key, coord);
            });
    }

    for (std::size_t i = 0; i < coords_.size(); ++i) {
        const RDB_ALL_DATA_PTR& coord = coords_[i];
        root_item_->BuildTree(coord, keys, getters);
    }
    endResetModel();
}

void RDBTreeModel::BuildRDBCheckTree() {
    beginResetModel();
    delete root_item_;
    root_item_ = new RDBTreeItem;
    search_list_.clear();
    total_list_.clear();
    cur_index_ = -1;
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const RDB_DATA_PTR& chip = chips_[i];
        if (!chip) continue;
        RDBTreeItem* item = root_item_->FindOrCreateChild(chip->name);
        item->count += chip->count;
        item->chip_index = chip->index;
    }
    endResetModel();
}

void RDBTreeModel::SetDBU(double dbu) {
    dbu_ = dbu;
}

RDB_ALL_DATA_LIST RDBTreeModel::GetCoordsList(
    const QModelIndex& modelIndex) const {
    RDB_ALL_DATA_LIST values;
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    if (!item || item == root_item_) return values;
    values.reserve(static_cast<std::size_t>(item->coords_map.size()));
    QMap<quint64, RDB_ALL_DATA_PTR>::const_iterator it =
        item->coords_map.constBegin();
    for (; it != item->coords_map.constEnd(); ++it) values.push_back(it.value());
    return values;
}

QStringList RDBTreeModel::GetHeaderList(
    const QModelIndex& modelIndex) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    return item ? item->headers.values() : QStringList();
}

QStringList RDBTreeModel::GetCompKey() const {
    QStringList keys;
    for (std::size_t i = 0; i < comp_keys_.size(); ++i) {
        keys.append(comp_keys_[i].toString());
    }
    return keys;
}

quint64 RDBTreeModel::GetChipIndex(
    const QModelIndex& modelIndex,
    bool* ok) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    const bool valid = item && item != root_item_;
    if (ok) *ok = valid;
    return valid ? item->chip_index : 0U;
}

void RDBTreeModel::SearchListBFS(const QString& text) {
    search_list_.clear();
    total_list_.clear();
    cur_index_ = -1;
    if (text.isEmpty()) return;
    CollectSearchIndexes(QModelIndex(), text);
    search_list_ = total_list_;
    if (!search_list_.empty()) cur_index_ = 0;
}

QModelIndex RDBTreeModel::SearchNext(int direction) {
    if (search_list_.empty()) return QModelIndex();
    if (cur_index_ < 0) cur_index_ = 0;
    else {
        const int size = static_cast<int>(search_list_.size());
        cur_index_ = (cur_index_ + (direction < 0 ? -1 : 1) + size) % size;
    }
    return search_list_[static_cast<std::size_t>(cur_index_)];
}

QModelIndex RDBTreeModel::GetSearchIndex() const {
    return cur_index_ >= 0 &&
        static_cast<std::size_t>(cur_index_) < search_list_.size()
        ? search_list_[static_cast<std::size_t>(cur_index_)] : QModelIndex();
}

int RDBTreeModel::SearchCount() const {
    return search_list_.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(search_list_.size());
}

int RDBTreeModel::SearchPosition() const {
    return cur_index_;
}

void RDBTreeModel::InitSearch(const QString& text) {
    SearchListBFS(text);
}

RDBTreeItem* RDBTreeModel::ItemFromIndex(
    const QModelIndex& modelIndex) const {
    return modelIndex.isValid()
        ? static_cast<RDBTreeItem*>(modelIndex.internalPointer())
        : root_item_;
}

QVariant RDBTreeModel::GroupValue(
    const QString& key,
    const RDB_ALL_DATA_PTR& coord) const {
    const QMap<QString, GetterFunc>::const_iterator getter =
        getter_func_.constFind(key);
    if (getter != getter_func_.constEnd()) return getter.value()(coord);
    return coord ? coord->PropertyValue(key) : QVariant();
}

void RDBTreeModel::CollectSearchIndexes(
    const QModelIndex& parentIndex,
    const QString& text) {
    const int rows = rowCount(parentIndex);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex current = index(row, 0, parentIndex);
        if (data(current, Qt::DisplayRole).toString().contains(
                text, Qt::CaseInsensitive)) {
            total_list_.push_back(current);
        }
        CollectSearchIndexes(current, text);
    }
}
