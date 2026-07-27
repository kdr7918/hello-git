#include "rdb_check_tree_model.hpp"

#include <QMap>

CheckTreeModel::CheckTreeModel(QObject* parent)
    : QAbstractItemModel(parent), database_(0), root_(new Node) {
    grouping_.push_back(GroupingDimension(GroupingDimension::CheckName));
}

QModelIndex CheckTreeModel::index(int row, int column, const QModelIndex& parentIndex) const {
    if (row < 0 || column < 0 || column >= columnCount() ||
        (parentIndex.isValid() && parentIndex.column() != 0)) {
        return QModelIndex();
    }
    Node* parentNode = nodeForIndex(parentIndex);
    if (!parentNode || row >= static_cast<int>(parentNode->children.size())) return QModelIndex();
    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex CheckTreeModel::parent(const QModelIndex& child) const {
    Node* node = nodeForIndex(child);
    if (!node || node == root_.get() || !node->parent || node->parent == root_.get()) {
        return QModelIndex();
    }
    return createIndex(node->parent->row, 0, node->parent);
}

int CheckTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.isValid() && parentIndex.column() != 0) return 0;
    Node* parentNode = nodeForIndex(parentIndex);
    return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int CheckTreeModel::columnCount(const QModelIndex& parentIndex) const {
    (void)parentIndex;
    return 2;
}

QVariant CheckTreeModel::data(const QModelIndex& index, int role) const {
    Node* node = nodeForIndex(index);
    if (!node || node == root_.get()) return QVariant();
    if (role == Qt::DisplayRole) {
        return index.column() == 0 ? QVariant(node->label) : QVariant(QString::number(node->count));
    }
    if (role == Qt::UserRole) return node->selection.identity;
    if (role == Qt::TextAlignmentRole && index.column() == 1) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant CheckTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    if (section == 0) return tr("Check Name");
    if (section == 1) return tr("Count");
    return QVariant();
}

Qt::ItemFlags CheckTreeModel::flags(const QModelIndex& index) const {
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

void CheckTreeModel::clearTree() {
    beginResetModel();
    database_ = 0;
    grouping_.clear();
    grouping_.push_back(GroupingDimension(GroupingDimension::CheckName));
    clearNodes();
    endResetModel();
}

void CheckTreeModel::rebuildIndex(const rdb::CheckIndexDatabase& index) {
    beginResetModel();
    database_ = 0;
    clearNodes();

    typedef QPair<qulonglong, QVector<int> > Group;
    QMap<QString, Group> grouped;
    for (std::size_t row = 0; row < index.checks.size(); ++row) {
        const rdb::CheckIndexEntry& entry = index.checks[row];
        const QString name = QString::fromStdString(entry.name);
        Group& group = grouped[name];
        group.first += entry.geometry_count;
        group.second.append(static_cast<int>(row));
    }
    for (QMap<QString, Group>::const_iterator it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        TreeSelection selection;
        selection.checkRows = it.value().second;
        selection.identity = checkIdentity(it.key());
        appendTreeRow(root_.get(), it.key(), it.value().first, selection);
    }
    endResetModel();
}

void CheckTreeModel::rebuildFull(const rdb::Database& database) {
    database_ = &database;
    rebuildFullTree();
}

bool CheckTreeModel::hasFullDatabase() const { return database_ != 0; }

void CheckTreeModel::resetGrouping() {
    grouping_.clear();
    grouping_.push_back(GroupingDimension(GroupingDimension::CheckName));
    if (database_) rebuildFullTree();
}

void CheckTreeModel::setGrouping(int depth, const GroupingDimension& dimension) {
    if (depth < 0 || depth > 2 || depth > static_cast<int>(grouping_.size())) return;
    if (depth == static_cast<int>(grouping_.size())) grouping_.push_back(dimension);
    else {
        grouping_[static_cast<std::size_t>(depth)] = dimension;
        grouping_.resize(static_cast<std::size_t>(depth + 1));
    }
    if (database_) rebuildFullTree();
}

const std::vector<GroupingDimension>& CheckTreeModel::grouping() const { return grouping_; }

QStringList CheckTreeModel::availableTagKeys() const {
    QStringList keys;
    if (!database_) return keys;
    for (std::vector<rdb::TaggedValue>::const_iterator it = database_->tagged_values.begin();
         it != database_->tagged_values.end(); ++it) {
        const QString key = QString::fromStdString(database_->strings.get(it->id).str());
        if (!key.isEmpty() && !keys.contains(key)) keys.append(key);
    }
    keys.sort();
    return keys;
}

bool CheckTreeModel::selectionForIndex(const QModelIndex& index, TreeSelection& selection) const {
    Node* node = nodeForIndex(index);
    if (!node || node == root_.get() || node->selection.identity.isEmpty()) return false;
    selection = node->selection;
    return true;
}

QString CheckTreeModel::selectionIdentity(const QModelIndex& index) const {
    TreeSelection selection;
    return selectionForIndex(index, selection) ? selection.identity : QString();
}

QModelIndex CheckTreeModel::indexForIdentity(const QString& identity) const {
    Node* node = nodesByIdentity_.value(identity, 0);
    return node ? createIndex(node->row, 0, node) : QModelIndex();
}

bool CheckTreeModel::matchesConditions(
    int checkRow,
    const rdb::Result& result,
    const std::vector<GroupCondition>& conditions) const {
    for (std::vector<GroupCondition>::const_iterator it = conditions.begin();
         it != conditions.end(); ++it) {
        if (!valuesForDimension(checkRow, result, it->dimension).contains(it->value)) return false;
    }
    return true;
}

void CheckTreeModel::rebuildFullTree() {
    if (!database_) return;
    beginResetModel();
    clearNodes();
    ResultList results;
    for (std::size_t check = 0; check < database_->rule_checks.size(); ++check) {
        const rdb::RuleCheck& rule = database_->rule_checks[check];
        for (rdb::Index offset = 0; offset < rule.results.count; ++offset) {
            results.append(qMakePair(static_cast<int>(check), rule.results.begin + offset));
        }
    }
    appendFullTreeLevel(root_.get(), 0, results, std::vector<GroupCondition>());
    endResetModel();
}

void CheckTreeModel::appendFullTreeLevel(Node* parent,
                                         int depth,
                                         const ResultList& results,
                                         const std::vector<GroupCondition>& conditions) {
    if (!database_ || depth >= static_cast<int>(grouping_.size())) return;
    QMap<QString, ResultList> groups;
    const GroupingDimension& dimension = grouping_[static_cast<std::size_t>(depth)];
    for (const QPair<int, rdb::Index>& reference : results) {
        const rdb::Result& result = database_->results.at(reference.second);
        const QStringList values = valuesForDimension(reference.first, result, dimension);
        for (const QString& value : values) groups[value].append(reference);
    }
    for (QMap<QString, ResultList>::const_iterator it = groups.constBegin(); it != groups.constEnd(); ++it) {
        std::vector<GroupCondition> childConditions = conditions;
        GroupCondition condition;
        condition.dimension = dimension;
        condition.value = it.key();
        childConditions.push_back(condition);
        TreeSelection selection;
        selection.conditions = childConditions;
        selection.identity = conditionIdentity(childConditions);
        Node* child = appendTreeRow(parent, it.key(),
                                    static_cast<qulonglong>(it.value().size()), selection);
        appendFullTreeLevel(child, depth + 1, it.value(), childConditions);
    }
}

CheckTreeModel::Node* CheckTreeModel::appendTreeRow(Node* parent,
                                                     const QString& label,
                                                     qulonglong count,
                                                     const TreeSelection& selection) {
    Node* parentNode = parent ? parent : root_.get();
    const int row = static_cast<int>(parentNode->children.size());
    parentNode->children.push_back(std::unique_ptr<Node>(new Node(parentNode, row)));
    Node* node = parentNode->children.back().get();
    node->label = label;
    node->count = count;
    node->selection = selection;
    if (!selection.identity.isEmpty()) nodesByIdentity_.insert(selection.identity, node);
    return node;
}

CheckTreeModel::Node* CheckTreeModel::nodeForIndex(const QModelIndex& index) const {
    if (!index.isValid()) return root_.get();
    if (index.model() != this) return 0;
    return static_cast<Node*>(index.internalPointer());
}

void CheckTreeModel::clearNodes() {
    root_->children.clear();
    nodesByIdentity_.clear();
}

QStringList CheckTreeModel::valuesForDimension(int checkRow,
                                                const rdb::Result& result,
                                                const GroupingDimension& dimension) const {
    QStringList values;
    if (!database_ || checkRow < 0 ||
        static_cast<std::size_t>(checkRow) >= database_->rule_checks.size()) return values;
    if (dimension.kind == GroupingDimension::CheckName) {
        values.append(QString::fromStdString(database_->strings.get(
            database_->rule_checks[static_cast<std::size_t>(checkRow)].name).str()));
        return values;
    }
    const rdb::Range ranges[] = { result.properties_before_geometry, result.properties_after_geometry };
    for (int rangeIndex = 0; rangeIndex < 2; ++rangeIndex) {
        const rdb::Range& range = ranges[rangeIndex];
        for (rdb::Index offset = 0; offset < range.count; ++offset) {
            const rdb::TaggedValue& tag = database_->tagged_values.at(range.begin + offset);
            if (QString::fromStdString(database_->strings.get(tag.id).str()) != dimension.key) continue;
            const QString value = QString::fromStdString(database_->strings.get(tag.payload).str());
            if (!values.contains(value)) values.append(value);
        }
    }
    if (values.isEmpty()) values.append(tr("(missing)"));
    return values;
}

QString CheckTreeModel::conditionIdentity(const std::vector<GroupCondition>& conditions) {
    QStringList items;
    for (std::vector<GroupCondition>::const_iterator it = conditions.begin();
         it != conditions.end(); ++it) {
        const QString prefix = it->dimension.kind == GroupingDimension::CheckName
            ? QStringLiteral("check") : QStringLiteral("tag:") + it->dimension.key;
        items.append(prefix + QStringLiteral("=") + it->value);
    }
    return items.join(QStringLiteral("\x1e"));
}

QString CheckTreeModel::checkIdentity(const QString& name) {
    std::vector<GroupCondition> conditions;
    GroupCondition condition;
    condition.dimension = GroupingDimension(GroupingDimension::CheckName);
    condition.value = name;
    conditions.push_back(condition);
    return conditionIdentity(conditions);
}
