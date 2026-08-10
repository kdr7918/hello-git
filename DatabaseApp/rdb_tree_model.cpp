#include "rdb_tree_model.hpp"

#include <QSet>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {

// 비어 있거나 유효하지 않은 grouping 값을 일관된 표시 key로 변환한다.
QString DisplayKey(const QVariant& value) {
    if (!value.isValid() || value.toString().isEmpty()) {
        return QStringLiteral("<none>");
    }
    return value.toString();
}

} // namespace

// title과 부모를 가진 Tree node를 만들고 부모 기준 depth를 계산한다.
RDBTreeItem::RDBTreeItem(const QVariant& itemTitle, RDBTreeItem* parent)
    : depth(parent ? parent->depth + 1 : 0),
      count(0U),
      chip_index(0U),
      title(itemTitle),
      m_parentItem(parent),
      exact_results(false),
      implicit_check_results(false) {}

// node가 단독 소유하는 모든 자식 node를 재귀적으로 해제한다.
RDBTreeItem::~RDBTreeItem() {
    for (std::size_t i = 0; i < m_childItems.size(); ++i) {
        delete m_childItems[i];
    }
}

// 같은 표시 문자열의 자식을 재사용하고 없을 때만 새 grouping node를 만든다.
RDBTreeItem* RDBTreeItem::FindOrCreateChild(const QVariant& itemTitle) {
    const QString key = DisplayKey(itemTitle);
    for (std::size_t i = 0; i < m_childItems.size(); ++i) {
        if (DisplayKey(m_childItems[i]->title) == key) {
            return m_childItems[i];
        }
    }
    RDBTreeItem* child = new RDBTreeItem(itemTitle, this);
    m_childItems.push_back(child);
    return child;
}

// 지정 행의 직접 자식을 범위 검사 후 반환한다.
RDBTreeItem* RDBTreeItem::Child(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < m_childItems.size()
        ? m_childItems[static_cast<std::size_t>(row)] : 0;
}

// 직접 자식 수를 Qt int 범위 안에서 반환한다.
int RDBTreeItem::ChildCount() const {
    return m_childItems.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(m_childItems.size());
}

// 부모의 자식 vector에서 현재 node의 QModelIndex 행 번호를 찾는다.
int RDBTreeItem::Row() const {
    if (!m_parentItem) return 0;
    for (std::size_t i = 0; i < m_parentItem->m_childItems.size(); ++i) {
        if (m_parentItem->m_childItems[i] == this) return static_cast<int>(i);
    }
    return 0;
}

// 현재 node의 부모를 반환한다.
RDBTreeItem* RDBTreeItem::Parent() const {
    return m_parentItem;
}

// node가 포함하는 CheckId와 ResultIndex를 누적하고 정확 선택임을 표시한다.
void RDBTreeItem::AddResult(
    rdb::CheckId checkId,
    rdb::Index resultIndex) {
    // BuildRDBTree는 CheckId 오름차순으로 진행하므로 같은 Check의 결과는
    // 항상 연속해서 들어온다. 전체 vector 검색 없이 마지막 값만 보면 된다.
    if (check_ids.empty() || check_ids.back() != checkId) {
        check_ids.push_back(checkId);
    }
    result_indices.push_back(resultIndex);
    exact_results = true;
    chip_index = checkId;
}

// 현재 depth의 자식을 정렬한 뒤 같은 작업을 하위 node에 재귀 적용한다.
void RDBTreeItem::SortChildren() {
    std::stable_sort(
        m_childItems.begin(), m_childItems.end(),
        [](const RDBTreeItem* left, const RDBTreeItem* right) {
            return QString::compare(
                DisplayKey(left ? left->title : QVariant()),
                DisplayKey(right ? right->title : QVariant()),
                Qt::CaseInsensitive) < 0;
        });
    for (std::size_t i = 0; i < m_childItems.size(); ++i) {
        m_childItems[i]->SortChildren();
    }
}

// Check Name 기본 grouping과 빈 root/Database를 가진 TreeModel을 만든다.
RDBTreeModel::RDBTreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      dbu_(0.0),
      cur_index_(-1),
      root_item_(new RDBTreeItem),
      database_(new rdb::Database),
      available_categories_cache_valid_(false),
      available_categories_tag_count_(0U) {
    comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    InitGetterFunc();
}

// Tree 전체 소유권의 시작점인 root node를 해제한다.
RDBTreeModel::~RDBTreeModel() {
    delete root_item_;
}

// parent node의 자식을 내부 포인터로 갖는 QModelIndex를 생성한다.
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

// 자식 node의 부모를 Qt가 요구하는 행·열 QModelIndex로 변환한다.
QModelIndex RDBTreeModel::parent(const QModelIndex& childIndex) const {
    if (!childIndex.isValid()) return QModelIndex();
    RDBTreeItem* child = ItemFromIndex(childIndex);
    RDBTreeItem* parentItem = child ? child->Parent() : 0;
    if (!parentItem || parentItem == root_item_) return QModelIndex();
    return createIndex(parentItem->Row(), 0, parentItem);
}

// parent node가 가진 직접 자식 수를 반환한다.
int RDBTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.isValid() && parentIndex.column() != KEY) return 0;
    RDBTreeItem* item = ItemFromIndex(parentIndex);
    return item ? item->ChildCount() : 0;
}

// grouping key와 결과 개수의 두 열 계약을 반환한다.
int RDBTreeModel::columnCount(const QModelIndex&) const {
    return CHECK_TREE_COL_COUNT;
}

// Tree node의 title·count와 Count 열 정렬 정보를 제공한다.
QVariant RDBTreeModel::data(
    const QModelIndex& modelIndex,
    int role) const {
    if (!modelIndex.isValid()) return QVariant();
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    if (!item) return QVariant();
    if (role == Qt::DisplayRole) {
        if (modelIndex.column() == KEY) return DisplayKey(item->title);
        if (modelIndex.column() == VALUE) {
            return QVariant::fromValue(static_cast<qulonglong>(item->count));
        }
    }
    if (role == Qt::TextAlignmentRole && modelIndex.column() == VALUE) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

// Tree 수평 헤더의 Key와 Count 이름을 제공한다.
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

// 유효한 Tree node만 선택·활성화 가능하게 한다.
Qt::ItemFlags RDBTreeModel::flags(const QModelIndex& modelIndex) const {
    return modelIndex.isValid()
        ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

// 원본 getter 초기화 API를 유지하되 Database 구현은 GroupValue를 직접 사용한다.
void RDBTreeModel::InitGetterFunc() {
    // 원본 API 이름은 유지하되 Database에서는 GroupValue가 ID로 값을 구한다.
}

// 공유 Database를 교체하고 Detail 완성 여부에 맞는 Tree 종류를 즉시 빌드한다.
void RDBTreeModel::SetDatabase(const RDB_DATABASE_PTR& database) {
    if (!database) throw std::invalid_argument("RDB Database is null");
    database_ = database;
    dbu_ = database_->database_precision;
    group_tag_ids_.clear();
    available_categories_cache_.clear();
    available_categories_cache_valid_ = false;
    available_categories_tag_count_ = 0U;
    if (database_->check_count() != 0U &&
        database_->loaded_check_count() == database_->check_count()) {
        BuildRDBTree();
    } else {
        BuildRDBCheckTree();
    }
}

// 현재 Tree가 참조하는 공유 Database 포인터를 반환한다.
RDB_DATABASE_PTR RDBTreeModel::GetDatabase() const {
    return database_;
}

// 전달 순서를 보존한 최대 3개 grouping key를 저장하고 Tree를 다시 빌드한다.
void RDBTreeModel::SetCompKeys(const QStringList& keys) {
    comp_keys_.clear();
    for (int i = 0; i < keys.size() && i < 3; ++i) {
        if (!keys[i].isEmpty()) comp_keys_.push_back(QVariant(keys[i]));
    }
    if (comp_keys_.empty()) {
        comp_keys_.push_back(QVariant(QStringLiteral("Check Name")));
    }
    if (database_->check_count() != 0U &&
        database_->loaded_check_count() == database_->check_count()) {
        BuildRDBTree();
    } else {
        BuildRDBCheckTree();
    }
}

// 완성된 Tree의 각 depth 자식을 표시 title 기준으로 정렬한다.
void RDBTreeModel::SortForGrouping() {
    if (root_item_) root_item_->SortChildren();
}

// 모든 Detail Result를 선택 key 순서에 따라 최대 3 Depth Tree로 그룹화한다.
void RDBTreeModel::BuildRDBTree() {
    // Check Name 하나만 선택한 기본 Tree는 Result를 전부 순회할 필요가 없다.
    // GetResultIndices()가 Check Range에서 기존과 같은 결과 목록을 생성한다.
    if (comp_keys_.size() == 1U &&
        comp_keys_.front().toString() == QStringLiteral("Check Name")) {
        BuildRDBCheckTree();
        return;
    }

    // 새 root 할당을 reset 전에 끝내 메모리 부족 시 기존 Tree를 그대로 보존한다.
    RDBTreeItem* const replacement = new RDBTreeItem;
    beginResetModel();
    delete root_item_;
    root_item_ = replacement;
    try {
        search_list_.clear();
        total_list_.clear();
        cur_index_ = -1;

        QStringList groupingKeys;
        groupingKeys.reserve(static_cast<int>(comp_keys_.size()));
        for (std::size_t depth = 0; depth < comp_keys_.size(); ++depth) {
            groupingKeys.append(comp_keys_[depth].toString());
        }

        // 선택된 Property key를 한 번만 StringId로 해석한다. 같은 문자열을
        // 가진 ID가 둘 이상인 외부 Database도 기존 문자열 비교와 같게 처리한다.
        group_tag_ids_.clear();
        QSet<rdb::StringId> visitedTagIds;
        for (std::size_t taggedIndex = 0;
             taggedIndex < database_->tagged_values.size(); ++taggedIndex) {
            const rdb::StringId id = database_->tagged_values[taggedIndex].id;
            if (visitedTagIds.contains(id)) continue;
            visitedTagIds.insert(id);
            const QString name = RDBString(database_->strings, id);
            if (name == QStringLiteral("Check Name") ||
                name == QStringLiteral("Type") ||
                name == QStringLiteral("ID") ||
                !groupingKeys.contains(name)) {
                continue;
            }
            group_tag_ids_[name].push_back(id);
        }

        // Check/Result를 한 번 순회하며 각 Result를 선택된 key 경로에 누적한다.
        for (std::size_t checkNumber = 0;
             checkNumber < database_->rule_checks.size(); ++checkNumber) {
            const rdb::CheckId checkId =
                static_cast<rdb::CheckId>(checkNumber);
            const rdb::RuleCheck& check = database_->rule_checks[checkNumber];
            if (!check.detail_loaded) continue;
            const QString checkName = groupingKeys.contains(
                QStringLiteral("Check Name"))
                ? RDBString(database_->strings, check.name) : QString();
            for (std::size_t offset = 0;
                 offset < check.results.count; ++offset) {
                const rdb::Index resultIndex = static_cast<rdb::Index>(
                    static_cast<std::size_t>(check.results.begin) + offset);
                RDBTreeItem* item = root_item_;
                for (std::size_t depth = 0;
                     depth < static_cast<std::size_t>(groupingKeys.size());
                     ++depth) {
                    const QString& key =
                        groupingKeys[static_cast<int>(depth)];
                    const QVariant value =
                        key == QStringLiteral("Check Name")
                        ? QVariant(checkName)
                        : GroupValue(key, checkId, resultIndex);
                    // 모든 prefix node가 자신의 하위 Result count와 선택 목록을 가진다.
                    item = item->FindOrCreateChild(value);
                    ++item->count;
                    item->AddResult(checkId, resultIndex);
                }
            }
        }
        SortForGrouping();
    } catch (...) {
        group_tag_ids_.clear();
        endResetModel();
        throw;
    }
    endResetModel();
}

// Detail 미완료 또는 Check Name 단일 grouping용 경량 1 Depth Tree를 만든다.
void RDBTreeModel::BuildRDBCheckTree() {
    RDBTreeItem* const replacement = new RDBTreeItem;
    beginResetModel();
    delete root_item_;
    root_item_ = replacement;
    try {
        search_list_.clear();
        total_list_.clear();
        cur_index_ = -1;
        // 완성 DB에서는 Check Range만으로 ResultIndex를 나중에 생성할 수 있다.
        const bool allLoaded = database_->check_count() != 0U &&
            database_->loaded_check_count() == database_->check_count();
        for (std::size_t i = 0; i < database_->rule_checks.size(); ++i) {
            const rdb::RuleCheck& check = database_->rule_checks[i];
            RDBTreeItem* item = root_item_->FindOrCreateChild(
                RDBString(database_->strings, check.name));
            item->count += check.current_result_count;
            item->chip_index = static_cast<quint64>(i);
            item->check_ids.push_back(static_cast<rdb::CheckId>(i));
            item->exact_results = allLoaded;
            item->implicit_check_results = allLoaded;
        }
        SortForGrouping();
    } catch (...) {
        endResetModel();
        throw;
    }
    endResetModel();
}

// 원본 인터페이스용 DBU 값을 저장한다.
void RDBTreeModel::SetDBU(double dbu) {
    dbu_ = dbu;
}

// 선택 node에 누적된 중복 없는 CheckId 목록을 반환한다.
std::vector<rdb::CheckId> RDBTreeModel::GetCheckIds(
    const QModelIndex& modelIndex) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    return item && item != root_item_
        ? item->check_ids : std::vector<rdb::CheckId>();
}

// node의 정확 Result 목록 또는 암시적 Check Range에서 만든 목록을 반환한다.
std::vector<rdb::Index> RDBTreeModel::GetResultIndices(
    const QModelIndex& modelIndex) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    if (!item || item == root_item_) return std::vector<rdb::Index>();
    if (!item->implicit_check_results) return item->result_indices;

    // 먼저 전체 크기를 계산해 반환 vector 재할당을 피한다.
    std::size_t resultCount = 0U;
    for (std::size_t i = 0; i < item->check_ids.size(); ++i) {
        resultCount += database_->check(item->check_ids[i]).results.count;
    }
    std::vector<rdb::Index> results;
    results.reserve(resultCount);
    for (std::size_t i = 0; i < item->check_ids.size(); ++i) {
        const rdb::RuleCheck& check = database_->check(item->check_ids[i]);
        for (std::size_t offset = 0; offset < check.results.count; ++offset) {
            results.push_back(static_cast<rdb::Index>(
                static_cast<std::size_t>(check.results.begin) + offset));
        }
    }
    return results;
}

// 선택 node가 정확한 Result 단위 grouping을 표현하는지 반환한다.
bool RDBTreeModel::HasExactResultSelection(
    const QModelIndex& modelIndex) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    return item && item != root_item_ && item->exact_results;
}

// 현재 적용된 grouping key를 원래 순서의 QStringList로 반환한다.
QStringList RDBTreeModel::GetCompKey() const {
    QStringList keys;
    for (std::size_t i = 0; i < comp_keys_.size(); ++i) {
        keys.append(comp_keys_[i].toString());
    }
    return keys;
}

// Database property 이름을 중복 제거·정렬하고 category 목록으로 캐시한다.
QStringList RDBTreeModel::GetAvailableCategories() const {
    // tagged_values 크기가 같으면 BG 완성 후 만든 cache를 그대로 재사용한다.
    if (available_categories_cache_valid_ &&
        available_categories_tag_count_ == database_->tagged_values.size()) {
        return available_categories_cache_;
    }
    QStringList categories;
    categories << QStringLiteral("Check Name") << QStringLiteral("Type");
    QSet<rdb::StringId> visitedIds;
    QSet<QString> visitedNames;
    visitedNames.insert(QStringLiteral("Check Name"));
    visitedNames.insert(QStringLiteral("Type"));
    for (std::size_t i = 0; i < database_->tagged_values.size(); ++i) {
        const rdb::StringId id = database_->tagged_values[i].id;
        if (visitedIds.contains(id)) continue;
        visitedIds.insert(id);
        const QString key = RDBString(
            database_->strings, id);
        if (!key.isEmpty() && !visitedNames.contains(key)) {
            visitedNames.insert(key);
            categories.append(key);
        }
    }
    if (categories.size() > 2) {
        QStringList tags = categories.mid(2);
        tags.sort(Qt::CaseInsensitive);
        categories = QStringList()
            << QStringLiteral("Check Name") << QStringLiteral("Type");
        categories.append(tags);
    }
    available_categories_cache_ = categories;
    available_categories_cache_valid_ = true;
    available_categories_tag_count_ = database_->tagged_values.size();
    return available_categories_cache_;
}

// node가 Check 하나만 대표할 때 해당 CheckId를 원본 chip index로 반환한다.
quint64 RDBTreeModel::GetChipIndex(
    const QModelIndex& modelIndex,
    bool* ok) const {
    RDBTreeItem* item = ItemFromIndex(modelIndex);
    const bool valid = item && item != root_item_ &&
        item->check_ids.size() == 1U;
    if (ok) *ok = valid;
    return valid ? static_cast<quint64>(item->check_ids.front()) : 0U;
}

// 현재 Tree 전체에서 문자열이 일치하는 QModelIndex 목록을 다시 만든다.
void RDBTreeModel::SearchListBFS(const QString& text) {
    search_list_.clear();
    // 원본 멤버는 호환성을 위해 유지하되 검색 결과는 한 vector에만 저장한다.
    total_list_.clear();
    cur_index_ = -1;
    if (text.isEmpty()) return;
    CollectSearchIndexes(QModelIndex(), text);
    if (!search_list_.empty()) cur_index_ = 0;
}

// 검색 결과를 앞뒤 방향으로 순환하고 새 현재 QModelIndex를 반환한다.
QModelIndex RDBTreeModel::SearchNext(int direction) {
    if (search_list_.empty()) return QModelIndex();
    if (cur_index_ < 0) {
        cur_index_ = 0;
    } else {
        const int size = static_cast<int>(search_list_.size());
        cur_index_ = (cur_index_ + (direction < 0 ? -1 : 1) + size) % size;
    }
    return search_list_[static_cast<std::size_t>(cur_index_)];
}

// 현재 검색 위치의 QModelIndex를 범위 검사 후 반환한다.
QModelIndex RDBTreeModel::GetSearchIndex() const {
    return cur_index_ >= 0 &&
        static_cast<std::size_t>(cur_index_) < search_list_.size()
        ? search_list_[static_cast<std::size_t>(cur_index_)] : QModelIndex();
}

// 검색 결과 수를 Qt int 범위 안에서 반환한다.
int RDBTreeModel::SearchCount() const {
    return search_list_.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(search_list_.size());
}

// 현재 검색 결과의 0 기반 위치를 반환한다.
int RDBTreeModel::SearchPosition() const {
    return cur_index_;
}

// 새 검색어로 검색 목록과 현재 위치를 초기화한다.
void RDBTreeModel::InitSearch(const QString& text) {
    SearchListBFS(text);
}

// QModelIndex 내부 포인터를 node로 변환하고 invalid index는 root로 취급한다.
RDBTreeItem* RDBTreeModel::ItemFromIndex(
    const QModelIndex& modelIndex) const {
    return modelIndex.isValid()
        ? static_cast<RDBTreeItem*>(modelIndex.internalPointer()) : root_item_;
}

// Check/Result와 grouping key 조합을 실제 표시 QVariant 값으로 변환한다.
QVariant RDBTreeModel::GroupValue(
    const QString& key,
    rdb::CheckId checkId,
    rdb::Index resultIndex) const {
    if (key == QStringLiteral("Check Name")) {
        return RDBString(database_->strings, database_->check(checkId).name);
    }
    if (static_cast<std::size_t>(resultIndex) >= database_->results.size()) {
        return QVariant();
    }
    const rdb::Result& result = database_->results[resultIndex];
    if (key == QStringLiteral("Type")) {
        return result.kind == rdb::ResultKind::Polygon
            ? QVariant(QStringLiteral("p")) : QVariant(QStringLiteral("e"));
    }
    if (key == QStringLiteral("ID")) {
        return QVariant::fromValue(static_cast<qulonglong>(resultIndex));
    }

    // Build 시 미리 해석한 tag ID 목록을 사용해 key 문자열 비교를 반복하지 않는다.
    const QMap<QString, std::vector<rdb::StringId> >::const_iterator ids =
        group_tag_ids_.constFind(key);
    if (ids == group_tag_ids_.constEnd()) return QVariant();

    QStringList values;
    for (std::size_t property = 0;
         property < result.properties.count; ++property) {
        const rdb::TaggedValue& tagged = database_->tagged_values[
            static_cast<std::size_t>(result.properties.begin) + property];
        if (std::find(ids.value().begin(), ids.value().end(), tagged.id) !=
            ids.value().end()) {
            const QString value = RDBString(database_->strings, tagged.payload);
            if (!value.isEmpty() && !values.contains(value)) {
                values.append(value);
            }
        }
    }
    return values.isEmpty() ? QVariant() : QVariant(values.join(
        QStringLiteral(" | ")));
}

// parent 이하 node를 순회해 title 부분 일치 QModelIndex를 검색 목록에 추가한다.
void RDBTreeModel::CollectSearchIndexes(
    const QModelIndex& parentIndex,
    const QString& text) {
    const int rows = rowCount(parentIndex);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex current = index(row, KEY, parentIndex);
        if (data(current, Qt::DisplayRole).toString().contains(
                text, Qt::CaseInsensitive)) {
            search_list_.push_back(current);
        }
        CollectSearchIndexes(current, text);
    }
}
