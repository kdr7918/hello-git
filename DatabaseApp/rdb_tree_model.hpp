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
    /// 표시 title과 부모 포인터를 가진 빈 Tree node를 생성한다.
    explicit RDBTreeItem(
        const QVariant& title = QVariant(),
        RDBTreeItem* parent = 0);
    /// 소유하는 모든 자식 node를 재귀적으로 해제한다.
    ~RDBTreeItem();

    /// 같은 표시 key의 자식을 찾고 없으면 새 node를 추가한다.
    RDBTreeItem* FindOrCreateChild(const QVariant& title);
    /// 지정한 행의 자식 node를 범위 검사 후 반환한다.
    RDBTreeItem* Child(int row) const;
    /// Qt int 범위로 제한한 자식 수를 반환한다.
    int ChildCount() const;
    /// 부모의 자식 목록에서 현재 node의 행 번호를 찾는다.
    int Row() const;
    /// 현재 node의 부모 포인터를 반환한다.
    RDBTreeItem* Parent() const;
    /// node가 대표하는 CheckId와 정확한 ResultIndex를 누적한다.
    void AddResult(rdb::CheckId checkId, rdb::Index resultIndex);
    /// 모든 depth의 자식 node를 표시 title 기준으로 안정 정렬한다.
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

    /// 기본 Check Name grouping을 가진 빈 TreeModel을 생성한다.
    explicit RDBTreeModel(QObject* parent = 0);
    /// root node와 그 하위 node 전체를 해제한다.
    ~RDBTreeModel() override;

    /// parent 아래의 행·열을 내부 TreeItem QModelIndex로 변환한다.
    QModelIndex index(
        int row,
        int column,
        const QModelIndex& parent = QModelIndex()) const override;
    /// 자식 QModelIndex의 부모 QModelIndex를 생성한다.
    QModelIndex parent(const QModelIndex& child) const override;
    /// parent node가 보유한 직접 자식 수를 반환한다.
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    /// Tree의 Key/Count 두 열을 반환한다.
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    /// Tree node의 title, count, 정렬 role 값을 반환한다.
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    /// Tree의 Key/Count 수평 헤더를 반환한다.
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;
    /// 유효한 Tree node에 선택 가능 flag를 부여한다.
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /// 원본 getter 초기화 인터페이스를 Database 구현에서도 유지한다.
    void InitGetterFunc();
    /// 공유 Database를 교체하고 적재 상태에 맞는 Tree를 다시 빌드한다.
    void SetDatabase(const RDB_DATABASE_PTR& database);
    /// 현재 Tree가 공유하는 Database를 반환한다.
    RDB_DATABASE_PTR GetDatabase() const;
    /// 최대 세 개 grouping key를 저장하고 Tree를 다시 빌드한다.
    void SetCompKeys(const QStringList& keys);
    /// root 아래 모든 자식을 화면 표시 순서로 정렬한다.
    void SortForGrouping();
    /// 완성 Detail 결과를 선택 grouping key에 따라 최대 3 Depth로 묶는다.
    void BuildRDBTree();
    /// Detail 순회 없이 Check Name과 count만으로 기본 Tree를 만든다.
    void BuildRDBCheckTree();
    /// 원본 인터페이스 호환용 DBU 값을 저장한다.
    void SetDBU(double dbu);
    /// 선택 node가 대표하는 CheckId 목록을 반환한다.
    std::vector<rdb::CheckId> GetCheckIds(const QModelIndex& index) const;
    /// 선택 node가 대표하는 정확한 ResultIndex 목록을 반환한다.
    std::vector<rdb::Index> GetResultIndices(const QModelIndex& index) const;
    /// node의 ResultIndex 선택이 전체 Check보다 더 구체적인지 반환한다.
    bool HasExactResultSelection(const QModelIndex& index) const;
    /// 현재 적용된 grouping key 목록을 반환한다.
    QStringList GetCompKey() const;
    /// Database에 존재하는 grouping category를 캐시해 반환한다.
    QStringList GetAvailableCategories() const;
    /// node가 Check 하나만 나타낼 때 그 CheckId를 반환한다.
    quint64 GetChipIndex(const QModelIndex& index, bool* ok = 0) const;
    /// 전체 Tree에서 대소문자 무시 문자열 검색 결과를 수집한다.
    void SearchListBFS(const QString& text);
    /// 검색 결과를 지정 방향으로 순환 이동한다.
    QModelIndex SearchNext(int direction);
    /// 현재 검색 위치의 QModelIndex를 반환한다.
    QModelIndex GetSearchIndex() const;
    /// Qt int 범위로 제한한 검색 결과 수를 반환한다.
    int SearchCount() const;
    /// 현재 검색 결과 위치를 0 기반으로 반환한다.
    int SearchPosition() const;

public slots:
    /// 새 검색어로 검색 목록과 현재 위치를 초기화한다.
    void InitSearch(const QString& text);

private:
    /// 유효 QModelIndex의 내부 node 또는 root node를 반환한다.
    RDBTreeItem* ItemFromIndex(const QModelIndex& index) const;
    /// Check/Result에서 지정 grouping key의 표시 값을 계산한다.
    QVariant GroupValue(
        const QString& key,
        rdb::CheckId checkId,
        rdb::Index resultIndex) const;
    /// parent 아래를 순회하며 일치 QModelIndex를 검색 목록에 추가한다.
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
