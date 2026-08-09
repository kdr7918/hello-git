#include "rdb_tree_model.hpp"

#include <QVariant>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {

QString ToQString(const rdb::StringTable& strings, rdb::StringId id) {
    if (id == rdb::invalid_string_id()) return QString();
    const rdb::StringRef text = strings.get(id);
    return text.data
        ? QString::fromUtf8(text.data, static_cast<int>(text.size))
        : QString();
}

template <typename T>
void AppendUnique(std::vector<T>& values, const T& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

} // namespace

struct RdbTreeModel::Node {
    explicit Node(Node* parent = 0, const QString& key = QString())
        : parent_(parent), key_(key), count_(0U), exact_results_(false) {}

    Node* FindOrAppendChild(const QString& key) {
        for (std::size_t i = 0; i < children_.size(); ++i) {
            if (children_[i]->key_ == key) return children_[i].get();
        }
        children_.push_back(std::unique_ptr<Node>(new Node(this, key)));
        return children_.back().get();
    }

    int RowInParent() const {
        if (!parent_) return 0;
        for (std::size_t i = 0; i < parent_->children_.size(); ++i) {
            if (parent_->children_[i].get() == this) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    Node* parent_;
    QString key_;
    std::size_t count_;
    std::vector<std::unique_ptr<Node> > children_;
    std::vector<rdb::CheckId> check_ids_;
    std::vector<rdb::Index> result_indices_;
    bool exact_results_;
};

RdbTreeModel::RdbTreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      database_(new rdb::Database),
      root_(new Node),
      grouping_categories_(QStringList() << CheckNameCategory()) {}

RdbTreeModel::~RdbTreeModel() {}

QModelIndex RdbTreeModel::index(
    int row,
    int column,
    const QModelIndex& parentIndex) const {
    if (row < 0 || column < 0 || column >= columnCount(parentIndex)) {
        return QModelIndex();
    }
    Node* const parentNode = NodeFromIndex(parentIndex);
    if (!parentNode ||
        static_cast<std::size_t>(row) >= parentNode->children_.size()) {
        return QModelIndex();
    }
    return createIndex(row, column, parentNode->children_[row].get());
}

QModelIndex RdbTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return QModelIndex();
    Node* const node = NodeFromIndex(child);
    if (!node || !node->parent_ || node->parent_ == root_.get()) {
        return QModelIndex();
    }
    return createIndex(
        node->parent_->RowInParent(), 0, node->parent_);
}

int RdbTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.isValid() && parentIndex.column() != 0) return 0;
    const Node* const node = NodeFromIndex(parentIndex);
    if (!node) return 0;
    const std::size_t size = node->children_.size();
    return size > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(size);
}

int RdbTreeModel::columnCount(const QModelIndex&) const {
    return 2;
}

QVariant RdbTreeModel::data(const QModelIndex& modelIndex, int role) const {
    if (!modelIndex.isValid()) return QVariant();
    const Node* const node = NodeFromIndex(modelIndex);
    if (!node) return QVariant();

    if (role == Qt::DisplayRole) {
        if (modelIndex.column() == 0) return node->key_;
        if (modelIndex.column() == 1) {
            return QVariant::fromValue(static_cast<qulonglong>(node->count_));
        }
    }
    if (role == Qt::TextAlignmentRole && modelIndex.column() == 1) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant RdbTreeModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    if (section == 0) return tr("Key");
    if (section == 1) return tr("Count");
    return QVariant();
}

Qt::ItemFlags RdbTreeModel::flags(const QModelIndex& modelIndex) const {
    return modelIndex.isValid()
        ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
        : Qt::NoItemFlags;
}

void RdbTreeModel::SetDatabase(
    const std::shared_ptr<rdb::Database>& database) {
    if (!database) throw std::invalid_argument("RDB Database is null");
    database_ = database;
    grouping_categories_ = QStringList() << CheckNameCategory();
    Rebuild();
}

std::shared_ptr<rdb::Database> RdbTreeModel::GetDatabase() const {
    return database_;
}

void RdbTreeModel::SetGroupingCategories(const QStringList& categories) {
    if (categories.isEmpty() || categories.size() > 3) {
        throw std::invalid_argument("RDB Tree grouping depth must be 1 to 3");
    }
    grouping_categories_ = categories;
    Rebuild();
}

QStringList RdbTreeModel::GetGroupingCategories() const {
    return grouping_categories_;
}

QStringList RdbTreeModel::GetAvailableCategories() const {
    QStringList categories;
    categories << CheckNameCategory();
    if (!database_) return categories;

    for (std::size_t i = 0; i < database_->tagged_values.size(); ++i) {
        const QString key = ToQString(
            database_->strings, database_->tagged_values[i].id);
        if (!key.isEmpty() && !categories.contains(key)) categories << key;
    }
    if (categories.size() > 2) {
        QStringList tags = categories.mid(1);
        tags.sort(Qt::CaseInsensitive);
        categories = QStringList() << CheckNameCategory();
        categories.append(tags);
    }
    return categories;
}

void RdbTreeModel::Rebuild() {
    beginResetModel();
    root_.reset(new Node);
    if (database_ && !database_->rule_checks.empty()) {
        const bool allLoaded =
            database_->loaded_rule_check_count == database_->check_count();
        if (!allLoaded ||
            (grouping_categories_.size() == 1 &&
             grouping_categories_.front() == CheckNameCategory())) {
            BuildCheckNameTree(allLoaded);
        } else {
            BuildGroupedTree();
        }
    }
    endResetModel();
}

std::vector<rdb::CheckId> RdbTreeModel::GetCheckIds(
    const QModelIndex& modelIndex) const {
    const Node* const node = NodeFromIndex(modelIndex);
    return node ? node->check_ids_ : std::vector<rdb::CheckId>();
}

std::vector<rdb::Index> RdbTreeModel::GetResultIndices(
    const QModelIndex& modelIndex) const {
    const Node* const node = NodeFromIndex(modelIndex);
    return node ? node->result_indices_ : std::vector<rdb::Index>();
}

bool RdbTreeModel::HasExactResultSelection(
    const QModelIndex& modelIndex) const {
    const Node* const node = NodeFromIndex(modelIndex);
    return node && node->exact_results_;
}

QString RdbTreeModel::CheckNameCategory() {
    return QStringLiteral("Check Name");
}

RdbTreeModel::Node* RdbTreeModel::NodeFromIndex(
    const QModelIndex& modelIndex) const {
    return modelIndex.isValid()
        ? static_cast<Node*>(modelIndex.internalPointer())
        : root_.get();
}

QString RdbTreeModel::CategoryValue(
    const QString& category,
    rdb::CheckId checkId,
    const rdb::Result* result) const {
    if (category == CheckNameCategory()) {
        return ToQString(database_->strings, database_->check(checkId).name);
    }
    if (!result) return tr("<없음>");

    QStringList values;
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(result->properties.count);
         ++i) {
        const rdb::TaggedValue& tagged = database_->tagged_values[
            static_cast<std::size_t>(result->properties.begin) + i];
        if (ToQString(database_->strings, tagged.id) == category) {
            const QString value = ToQString(database_->strings, tagged.payload);
            if (!value.isEmpty() && !values.contains(value)) values << value;
        }
    }
    return values.isEmpty() ? tr("<없음>") : values.join(QStringLiteral(" | "));
}

void RdbTreeModel::BuildCheckNameTree(bool allLoaded) {
    for (std::size_t i = 0; i < database_->rule_checks.size(); ++i) {
        const rdb::CheckId checkId = static_cast<rdb::CheckId>(i);
        const rdb::RuleCheck& check = database_->rule_checks[i];
        Node* const node = root_->FindOrAppendChild(
            ToQString(database_->strings, check.name));
        node->count_ += static_cast<std::size_t>(check.current_result_count);
        AppendUnique(node->check_ids_, checkId);
        node->exact_results_ = allLoaded;
        if (allLoaded) {
            for (std::size_t result = 0;
                 result < static_cast<std::size_t>(check.results.count);
                 ++result) {
                node->result_indices_.push_back(static_cast<rdb::Index>(
                    static_cast<std::size_t>(check.results.begin) + result));
            }
        }
    }
}

void RdbTreeModel::BuildGroupedTree() {
    for (std::size_t checkIndex = 0;
         checkIndex < database_->rule_checks.size();
         ++checkIndex) {
        const rdb::CheckId checkId = static_cast<rdb::CheckId>(checkIndex);
        const rdb::RuleCheck& check = database_->rule_checks[checkIndex];
        for (std::size_t resultOffset = 0;
             resultOffset < static_cast<std::size_t>(check.results.count);
             ++resultOffset) {
            const rdb::Index resultIndex = static_cast<rdb::Index>(
                static_cast<std::size_t>(check.results.begin) + resultOffset);
            const rdb::Result& result = database_->results[resultIndex];
            Node* node = root_.get();
            for (int depth = 0; depth < grouping_categories_.size(); ++depth) {
                node = node->FindOrAppendChild(CategoryValue(
                    grouping_categories_[depth], checkId, &result));
                ++node->count_;
                AppendUnique(node->check_ids_, checkId);
                node->result_indices_.push_back(resultIndex);
                node->exact_results_ = true;
            }
        }
    }
}
