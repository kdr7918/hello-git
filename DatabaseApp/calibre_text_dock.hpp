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

    explicit CalibreTextDock(QWidget* parent = 0);
    ~CalibreTextDock() override;

    void SetType(RDB_TYPE type);
    void ParseRDBCheck(
        const QString& filePath,
        const ProgressCallback& progressCallback = ProgressCallback());

public slots:
    void OnClickedTreeView(const QModelIndex& index);
    void OnClickedChipNameTableView(const QModelIndex& index);
    void OnClickedCoordsTableView(const QModelIndex& index);
    void OnUpdateCoordsTable(
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    void OnUpdateCoordinateTableView();
    void OnCustomContextMenuRequested(const QPoint& position);
    void OnChangedCursor(quint64 index);
    void OnClearAllData();
    void OnCompleteBgParsing();
    void OnTreeHeaderClicked(int section);
    void OnMenuClicked(QAction* action);
    void OnSearchNextItem();
    void OnCompleteCheckParsing(quint64 checkIndex);

signals:
    void CheckIndexProgress(int value);
    void UpdateCoordsTable(
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    void UpdateCoordinateTableView();
    void ChangedCursor(quint64 index);
    void CompleteCheckParsing(quint64 checkIndex);
    void ParsingFailed(const QString& message);

private slots:
    void OnCheckIndexProgress(int value);
    void OnCompleteCheckIndex(const RDB_DATABASE_PTR& database);
    void OnCompleteBackgroundDatabase(const RDB_DATABASE_PTR& database);
    void OnStartCheckParsing(quint64 requestId, quint64 checkIndex);
    void OnSelectedBatchReady(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    void OnFinishCheckParsing(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_CHECK_DETAIL_PTR& detail);
    void OnSelectionParsingComplete(quint64 requestId);
    void OnSelectionParsingFailed(
        quint64 requestId,
        const QString& message);
    void OnSelectionParsingCancelled(quint64 requestId);
    void OnWorkerFailed(const QString& message);
    void OnWorkerCancelled();

private:
    void InitCheckTable();
    void InitCheckTree();
    void InitCoordTable();
    void InitContextMenu();
    void InitSignalSlot();
    void InitUI();
    void Clear();
    void StartDetailParser();
    void StartSelectionParser(const std::vector<rdb::CheckId>& checkIds);
    void StopWorkers();
    void StopSelectionParser();
    void DestroyIndexWorker();
    void DestroyDetailWorker();
    void DestroySelectionWorker();
    void SetTreeGroupingEnabled(bool enabled);
    void SelectFirstNavigationItem();
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
