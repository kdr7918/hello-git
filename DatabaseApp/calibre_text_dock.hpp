#ifndef DATABASE_APP_CALIBRE_TEXT_DOCK_HPP
#define DATABASE_APP_CALIBRE_TEXT_DOCK_HPP

#include "rdb_model.hpp"
#include "rdb_parser_workers.hpp"
#include "rdb_tree_model.hpp"

#include <QDockWidget>
#include <QSet>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QAction;
class QMenu;
class QModelIndex;
class QPoint;
class QStyledItemDelegate;
class QThread;

namespace Ui {
class CalibreTextDock;
}

class CalibreTextDock : public QDockWidget {
    Q_OBJECT

public:
    enum RDB_TYPE {
        COORDS_ONLY = 1,
        ALL_PARAMS = 2
    };

    enum CHECK_TABLE_COL {
        ID = 0,
        NAME,
        COUNT,
        SEEK
    };

    enum CHECK_TREE_COL {
        KEY = 0,
        VALUE
    };

    typedef std::function<void(int)> ProgressCallback;

    static const int TREE_VIEW_KEY;
    static const int TREE_VIEW_COUNT;
    static int CALIBRE_TEXT_DOCK_NUM;

    /// Designer UI와 모델, delegate, signal 연결을 초기화한다.
    explicit CalibreTextDock(QWidget* parent = 0);
    /// 진행 중 worker를 interrupt·join한 뒤 UI를 해제한다.
    ~CalibreTextDock() override;

    /// Coordinates 전용 또는 전체 파라미터 UI 모드를 선택한다.
    void SetType(RDB_TYPE type);
    /// 기존 작업을 정리하고 Check Index worker를 새 파일에 대해 시작한다.
    void ParseRDBCheck(
        const QString& filePath,
        const ProgressCallback& progressCallback = ProgressCallback());

public slots:
    /// Tree 행에 속한 Check를 선택 파싱하거나 완성 DB에서 즉시 표시한다.
    void OnClickedTreeView(const QModelIndex& index);
    /// Check Table 행 하나를 선택 파싱하거나 완성 DB에서 즉시 표시한다.
    void OnClickedChipNameTableView(const QModelIndex& index);
    /// 좌표 행 선택을 외부 cursor 변경 신호로 전달한다.
    void OnClickedCoordsTableView(const QModelIndex& index);
    /// 선택 parser의 한 Detail 배치를 좌표 모델에 반영한다.
    void OnUpdateCoordsTable(
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    /// 좌표 Table viewport를 갱신한다.
    void OnUpdateCoordinateTableView();
    /// BG 완료 후에만 Tree grouping 메뉴를 표시한다.
    void OnCustomContextMenuRequested(const QPoint& position);
    /// 외부 확장을 위한 cursor 변경 hook을 유지한다.
    void OnChangedCursor(quint64 index);
    /// 사용자 요청으로 worker와 모든 모델 데이터를 초기화한다.
    void OnClearAllData();
    /// 완성 BG Database를 모든 모델에 즉시 교체한다.
    void OnCompleteBgParsing();
    /// Tree key header 클릭 시 grouping 메뉴를 표시한다.
    void OnTreeHeaderClicked(int section);
    /// action에 저장된 순서대로 최대 3개 grouping key를 적용한다.
    void OnMenuClicked(QAction* action);
    /// 검색어 변경 또는 이전/다음 방향에 따라 Tree 검색 위치를 이동한다.
    void OnSearchNextItem();
    /// 한 선택 Check 완료를 외부에서 확장할 수 있는 hook으로 유지한다.
    void OnCompleteCheckParsing(quint64 checkIndex);

signals:
    /// 최초 Check Index 스캔 진행률을 알린다.
    void CheckIndexProgress(int value);
    /// 선택 parser 배치를 GUI thread의 좌표 모델 갱신 경로로 전달한다.
    void UpdateCoordsTable(
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    /// 좌표 Table viewport 갱신을 요청한다.
    void UpdateCoordinateTableView();
    /// 선택된 Check 또는 Result cursor 변경을 알린다.
    void ChangedCursor(quint64 index);
    /// 선택 Check 하나의 Detail 적재 완료를 알린다.
    void CompleteCheckParsing(quint64 checkIndex);
    /// 파싱·모델·UI 처리 중 발생한 오류 메시지를 전달한다.
    void ParsingFailed(const QString& message);

private slots:
    /// 활성 generation의 progress callback과 signal을 GUI thread에서 실행한다.
    void OnCheckIndexProgress(int value);
    /// Index DB를 모델에 연결하고 전체 BG Detail parser를 시작한다.
    void OnCompleteCheckIndex(const RDB_DATABASE_PTR& database);
    /// 전체 BG worker 결과를 보관한 뒤 최종 모델 교체를 수행한다.
    void OnCompleteBackgroundDatabase(const RDB_DATABASE_PTR& database);
    /// 현재 선택 요청의 Check rollback checkpoint를 연다.
    void OnStartCheckParsing(quint64 requestId, quint64 checkIndex);
    /// 현재 선택 요청의 배치만 좌표 갱신 신호로 전달한다.
    void OnSelectedBatchReady(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    /// 현재 선택 Check 메타데이터를 검증하고 적재를 확정한다.
    void OnFinishCheckParsing(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_CHECK_DETAIL_PTR& detail);
    /// 현재 선택 요청 worker의 정상 완료를 정리한다.
    void OnSelectionParsingComplete(quint64 requestId);
    /// 현재 선택 요청을 rollback하고 오류를 전달한다.
    void OnSelectionParsingFailed(
        quint64 requestId,
        const QString& message);
    /// 현재 선택 요청의 취소 결과를 rollback하고 worker를 정리한다.
    void OnSelectionParsingCancelled(quint64 requestId);
    /// Index/BG 오류 시 관련 작업을 중단하고 공통 오류를 전달한다.
    void OnWorkerFailed(const QString& message);
    /// 기존 인터페이스용 worker 취소 정리 slot을 유지한다.
    void OnWorkerCancelled();

private:
    /// Check 이름 Table의 모델·delegate·선택 정책을 설정한다.
    void InitCheckTable();
    /// grouping Tree의 모델·선택 정책과 초기 메뉴 정책을 설정한다.
    void InitCheckTree();
    /// 좌표 Table의 모델·delegate·선택 정책을 설정한다.
    void InitCoordTable();
    /// category 순열을 선택할 수 있는 최대 3 Depth 계층 메뉴를 만든다.
    void InitContextMenu();
    /// Designer widget과 Dock signal/slot을 한 번 연결한다.
    void InitSignalSlot();
    /// 세 View와 splitter, 검색 표시를 초기화한다.
    void InitUI();
    /// generation을 갱신하고 worker·모델·검색 상태를 비운다.
    void Clear();
    /// Index 완료 후 전체 Detail BG worker를 전용 thread에서 시작한다.
    void StartDetailParser();
    /// BG 완료 전 선택 Check 전용 임시 DB와 worker를 시작한다.
    void StartSelectionParser(const std::vector<rdb::CheckId>& checkIds);
    /// Index·BG·선택 worker에 interrupt를 주고 모두 join한다.
    void StopWorkers();
    /// 선택 요청 번호를 무효화하고 선택 worker를 join한다.
    void StopSelectionParser();
    /// 종료된 Index worker/thread 포인터와 interrupt 상태를 정리한다.
    void DestroyIndexWorker();
    /// 종료된 전체 BG worker/thread 포인터와 interrupt 상태를 정리한다.
    void DestroyDetailWorker();
    /// 종료된 선택 worker/thread 포인터와 interrupt 상태를 정리한다.
    void DestroySelectionWorker();
    /// BG 완료 상태에 따라 Tree header context menu 정책을 설정한다.
    void SetTreeGroupingEnabled(bool enabled);
    /// 현재 UI 모드의 첫 탐색 행을 선택해 초기 내용을 표시한다.
    void SelectFirstNavigationItem();
    /// 현재 Tree 검색 위치와 전체 개수를 label/button에 반영한다.
    void UpdateSearchLabel();

    Ui::CalibreTextDock* ui_;
    std::shared_ptr<std::atomic<bool> > index_interrupt_;
    std::shared_ptr<std::atomic<bool> > detail_interrupt_;
    std::shared_ptr<std::atomic<bool> > selection_interrupt_;
    bool is_parse_complete_;
    int dock_id_;
    RDB_TYPE type_;
    QString file_path_;
    QSet<QString> parse_header_list_;
    QString search_text_;
    ProgressCallback progress_callback_;
    QMenu* header_menu_;
    RDBModel* chip_model_;
    RDBModel* coord_model_;
    // BG 완료 후 세 모델은 완성 Database를 공유한다. 완료 전 coord_model_은
    // 선택 파싱 전용 임시 Database를 사용해 BG worker와 쓰기 영역을 분리한다.
    RDBModel* bg_model_;
    RDBTreeModel* tree_model_;
    QStyledItemDelegate* chip_delegate_;
    QStyledItemDelegate* coord_delegate_;
    QThread* index_thread_;
    BgParser* bg_parser_;
    QThread* detail_thread_;
    RDBBackgroundParser* detail_parser_;
    QThread* selection_thread_;
    RDBDetailParser* selection_parser_;
    quint64 selection_request_id_;
    quint64 parse_generation_;
    RDB_DATABASE_PTR completed_database_;
};

#endif // DATABASE_APP_CALIBRE_TEXT_DOCK_HPP
