#ifndef DATABASE_APP_RDB_MODEL_HPP
#define DATABASE_APP_RDB_MODEL_HPP

#include "rdb_database_support.hpp"

#include <QAbstractTableModel>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

class RDBModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum MODEL_TYPE {
        CHIP_TABLE = 0,
        COORDS_ONLY = 1,
        ALL_PARAMS = 2
    };

    enum CHECK_TABLE_COL {
        ID = 0,
        NAME,
        COUNT,
        SEEK,
        CHECK_TABLE_COL_COUNT
    };

    /// 지정된 표시 형식으로 빈 TableModel을 생성한다.
    explicit RDBModel(MODEL_TYPE type, QObject* parent = 0);

    /// 현재 선택 범위에서 Qt가 표시할 수 있는 행 수를 반환한다.
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    /// Check/좌표/전체 파라미터 형식에 맞는 열 수를 반환한다.
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    /// 요청된 셀의 Check 또는 Detail 값을 지연 생성한다.
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    /// 고정 열과 동적 파라미터 열의 헤더를 반환한다.
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    /// 진행 중 적재를 취소하고 빈 Database와 선택 상태로 초기화한다.
    void Clear();
    /// 모델 표시 형식을 바꾸고 동적 헤더를 다시 계산한다.
    void SetType(MODEL_TYPE type);
    /// 현재 모델 표시 형식을 반환한다.
    MODEL_TYPE GetType() const;
    /// 모델이 참조할 공유 Database를 교체하고 선택 상태를 초기화한다.
    void SetDatabase(const RDB_DATABASE_PTR& database);
    /// 현재 모델이 공유하는 Database를 반환한다.
    RDB_DATABASE_PTR GetDatabase() const;

    /// 한 Check의 Detail 적재를 시작하고 rollback checkpoint를 저장한다.
    void BeginCheckLoad(rdb::CheckId checkId);
    /// Parser 배치를 공용 배열에 연속 저장하고 필요한 View 행을 알린다.
    void AppendCoords(
        rdb::CheckId checkId,
        const RDB_DETAIL_BATCH_PTR& batch);
    /// Check 메타데이터를 검증·완성하고 Detail 적재를 확정한다.
    void FinishCheckLoad(
        rdb::CheckId checkId,
        const rdb::CheckDetail& detail);
    /// 진행 중인 Detail 적재를 checkpoint 상태로 되돌린다.
    void CancelCheckLoad();

    /// 하나의 Check만 좌표 Table의 활성 범위로 설정한다.
    void SetActiveCheck(quint64 checkIndex);
    /// 여러 Check의 결과 범위를 입력 순서대로 활성화한다.
    void SetActiveChecks(const std::vector<rdb::CheckId>& checkIds);
    /// Tree grouping이 지정한 정확한 결과 인덱스 목록을 활성화한다.
    void SetActiveResults(
        const std::vector<rdb::CheckId>& checkIds,
        const std::vector<rdb::Index>& resultIndices);
    /// Check Table의 View 행을 Database CheckId로 변환한다.
    rdb::CheckId CheckIdAt(int row) const;
    /// 좌표 Table의 View 행을 Database ResultIndex로 변환한다.
    rdb::Index ResultIndexAt(int row) const;
    /// 지금까지 intern된 Detail property 이름을 반환한다.
    QStringList AvailableHeaders() const;
    /// Database 좌표 정밀도(DBU)를 반환한다.
    double DBU() const;
    /// Database top-cell 이름을 반환한다.
    QString TopCell() const;

    /// 현재 활성 선택의 전체 행 수를 size_t 범위로 반환한다.
    std::size_t TotalRowCount() const;
    /// int 행 제한을 넘는 데이터를 탐색할 때 사용할 시작 offset을 반환한다.
    std::size_t RowOffset() const;
    /// View에 노출할 첫 source 행 offset을 설정한다.
    void SetRowOffset(std::size_t offset);

private:
    /// 전체 행 수를 Qt int 행 범위에 맞춰 제한한다.
    int VisibleRowCount(std::size_t totalRows) const;
    /// 현재 View 행에 row offset을 적용한 source 행을 계산한다.
    std::size_t SourceRow(int modelRow) const;
    /// 활성 Check/result 선택에서 source 행의 ResultIndex를 찾는다.
    rdb::Index ResultIndexForSource(std::size_t source) const;
    /// View 행에 대응하는 Result 포인터를 검증 후 반환한다.
    const rdb::Result* ResultAt(int modelRow) const;
    /// 활성 결과에 존재하는 property 열 목록을 원래 등장 순서로 재구성한다.
    void RebuildActiveHeaders();
    /// 같은 property 이름을 재사용하도록 StringId를 intern한다.
    rdb::StringId InternTagName(const std::string& name);
    /// Check Table 셀의 role별 값을 만든다.
    QVariant CheckIndexData(const QModelIndex& index, int role) const;
    /// Detail Table 셀의 role별 값을 만든다.
    QVariant DetailData(const QModelIndex& index, int role) const;
    /// Polygon/Edge geometry를 기존 좌표 문자열 형식으로 직렬화한다.
    QString CoordinateText(const rdb::Result& value) const;

    MODEL_TYPE type_;
    RDB_DATABASE_PTR database_;
    std::vector<rdb::CheckId> active_check_ids_;
    std::vector<rdb::Index> active_result_indices_;
    bool exact_result_selection_;
    std::vector<rdb::StringId> active_headers_;
    std::vector<rdb::StringId> interned_tag_names_;
    std::size_t row_offset_;

    bool detail_load_active_;
    rdb::CheckId loading_check_id_;
    std::unique_ptr<rdb::StringTable::Checkpoint> string_checkpoint_;
    rdb::RuleCheck original_check_;
    std::size_t result_checkpoint_;
    std::size_t vertex_checkpoint_;
    std::size_t edge_checkpoint_;
    std::size_t tagged_value_checkpoint_;
    std::size_t check_text_checkpoint_;
    std::size_t loaded_check_checkpoint_;
    std::size_t interned_tag_checkpoint_;
};

#endif // DATABASE_APP_RDB_MODEL_HPP
