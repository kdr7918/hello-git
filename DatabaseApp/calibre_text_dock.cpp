#include "calibre_text_dock.hpp"

#include "ui_calibre_text_dock.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCursor>
#include <QHeaderView>
#include <QList>
#include <QLineEdit>
#include <QMenu>
#include <QModelIndex>
#include <QPoint>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QThread>
#include <QTreeView>

#include <stdexcept>

namespace {

// grouping 적용 action에 key 경로를 넣고 Dock의 적용 slot을 직접 연결한다.
void ConfigureGroupingAction(
    CalibreTextDock* dock,
    QAction* action,
    const QStringList& keys,
    const QStringList& selected) {
    if (!dock || !action) return;
    action->setData(QVariant(keys));
    action->setCheckable(true);
    action->setChecked(keys == selected);
    QObject::connect(
        action,
        &QAction::triggered,
        dock,
        [dock, action](bool) {
            dock->OnMenuClicked(action);
        });
}

// submenu가 실제로 열릴 때 현재 prefix와 다음 key 선택지를 지연 생성한다.
void PrepareGroupingMenu(
    CalibreTextDock* dock,
    QMenu* menu,
    const QStringList& prefix,
    const QStringList& remaining,
    const QStringList& selected) {
    if (!dock || !menu || prefix.isEmpty()) return;

    QAction* const placeholder = menu->addAction(QStringLiteral("..."));
    placeholder->setEnabled(false);
    QObject::connect(
        menu,
        &QMenu::aboutToShow,
        dock,
        [dock, menu, prefix, remaining, selected]() {
            if (menu->property("rdb_grouping_populated").toBool()) return;
            menu->setProperty("rdb_grouping_populated", true);
            menu->clear();

            QAction* applyPrefix = menu->addAction(
                prefix.join(QStringLiteral(", ")));
            ConfigureGroupingAction(
                dock, applyPrefix, prefix, selected);

            if (prefix.size() >= 3 || remaining.isEmpty()) return;
            menu->addSeparator();

            for (int i = 0; i < remaining.size(); ++i) {
                const QString candidate = remaining[i];
                QStringList nextPrefix = prefix;
                nextPrefix.append(candidate);
                QStringList nextRemaining = remaining;
                nextRemaining.removeAt(i);

                if (nextPrefix.size() < 3 && !nextRemaining.isEmpty()) {
                    QMenu* nextMenu = menu->addMenu(candidate);
                    PrepareGroupingMenu(
                        dock,
                        nextMenu,
                        nextPrefix,
                        nextRemaining,
                        selected);
                } else {
                    QAction* leaf = menu->addAction(candidate);
                    ConfigureGroupingAction(
                        dock, leaf, nextPrefix, selected);
                }
            }
        });
}

} // namespace

const int CalibreTextDock::TREE_VIEW_KEY = 0;
const int CalibreTextDock::TREE_VIEW_COUNT = 1;
int CalibreTextDock::CALIBRE_TEXT_DOCK_NUM = 0;

// Designer UI, 세 모델, 세 worker 계열의 초기 상태와 queued 전달 타입을 준비한다.
CalibreTextDock::CalibreTextDock(QWidget* parent)
    : QDockWidget(parent),
      ui_(new Ui::CalibreTextDock),
      is_parse_complete_(false),
      dock_id_(++CALIBRE_TEXT_DOCK_NUM),
      type_(COORDS_ONLY),
      header_menu_(new QMenu(this)),
      chip_model_(new RDBModel(RDBModel::CHIP_TABLE, this)),
      coord_model_(new RDBModel(RDBModel::COORDS_ONLY, this)),
      bg_model_(new RDBModel(RDBModel::ALL_PARAMS, this)),
      tree_model_(new RDBTreeModel(this)),
      chip_delegate_(new QStyledItemDelegate(this)),
      coord_delegate_(new QStyledItemDelegate(this)),
      index_thread_(0),
      bg_parser_(0),
      detail_thread_(0),
      detail_parser_(0),
      selection_thread_(0),
      selection_parser_(0),
      selection_request_id_(0U),
      parse_generation_(0U) {
    // shared_ptr 기반 결과가 thread 경계를 넘어 queued signal로 전달되게 등록한다.
    qRegisterMetaType<RDB_DATABASE_PTR>("RDB_DATABASE_PTR");
    qRegisterMetaType<RDB_DETAIL_BATCH_PTR>("RDB_DETAIL_BATCH_PTR");
    qRegisterMetaType<RDB_CHECK_DETAIL_PTR>("RDB_CHECK_DETAIL_PTR");
    ui_->setupUi(this);
    header_menu_->setObjectName(QStringLiteral("tree_grouping_menu"));
    InitUI();
    InitSignalSlot();
    SetType(type_);
}

// 소멸 전에 모든 worker에 interrupt를 주고 join해 UI·모델 접근 경쟁을 없앤다.
CalibreTextDock::~CalibreTextDock() {
    StopWorkers();
    delete ui_;
}

// 표시 모드에 맞춰 탐색 View와 DetailModel 열 구성을 함께 전환한다.
void CalibreTextDock::SetType(RDB_TYPE type) {
    type_ = type;
    coord_model_->SetType(
        type_ == ALL_PARAMS
            ? RDBModel::ALL_PARAMS : RDBModel::COORDS_ONLY);
    ui_->chip_name_table_view->setVisible(type_ == COORDS_ONLY);
    ui_->rdb_tree_view->setVisible(type_ == ALL_PARAMS);
    ui_->search_edit->setVisible(type_ == ALL_PARAMS);
    ui_->next_btn->setVisible(type_ == ALL_PARAMS);
    ui_->prev_btn->setVisible(type_ == ALL_PARAMS);
    ui_->search_index_label->setVisible(type_ == ALL_PARAMS);
}

// 기존 세대 작업을 정리한 뒤 Check Index parser를 전용 thread에서 시작한다.
void CalibreTextDock::ParseRDBCheck(
    const QString& filePath,
    const ProgressCallback& progressCallback) {
    if (filePath.isEmpty()) {
        throw std::invalid_argument("RDB file path must not be empty");
    }
    Clear();
    file_path_ = filePath;
    progress_callback_ = progressCallback;
    const quint64 generation = parse_generation_;
    try {
        index_interrupt_.reset(new std::atomic<bool>(false));
        index_thread_ = new QThread;
        bg_parser_ = new BgParser(this, file_path_, index_interrupt_);

        connect(index_thread_, &QThread::started,
                bg_parser_, &BgParser::run);
        connect(index_thread_, &QThread::finished,
                bg_parser_, &QObject::deleteLater);
        // generation 검사는 이전 파일의 늦은 queued signal이 새 상태를 덮지 못하게 한다.
        connect(bg_parser_, &BgParser::ProgressChanged,
                this, [this, generation](int value) {
                    if (generation == parse_generation_) {
                        OnCheckIndexProgress(value);
                    }
                });
        // 완료·실패·취소 어느 경로든 worker 쪽에서 thread event loop를 종료한다.
        connect(bg_parser_, &BgParser::CompleteBgParsing,
                this, [this, generation](const RDB_DATABASE_PTR& database) {
                    if (generation == parse_generation_) {
                        OnCompleteCheckIndex(database);
                    }
                });
        connect(bg_parser_, &BgParser::ParsingFailed,
                this, [this, generation](const QString& message) {
                    if (generation == parse_generation_) {
                        OnWorkerFailed(message);
                    }
                });
        connect(bg_parser_, &BgParser::ParsingCancelled,
                this, [this, generation]() {
                    if (generation == parse_generation_) {
                        DestroyIndexWorker();
                    }
                });
        connect(bg_parser_, &BgParser::CompleteBgParsing,
                index_thread_, &QThread::quit, Qt::DirectConnection);
        connect(bg_parser_, &BgParser::ParsingFailed,
                index_thread_, &QThread::quit, Qt::DirectConnection);
        connect(bg_parser_, &BgParser::ParsingCancelled,
                index_thread_, &QThread::quit, Qt::DirectConnection);
        bg_parser_->moveToThread(index_thread_);
        index_thread_->start();
    } catch (const std::exception& error) {
        OnWorkerFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        OnWorkerFailed(QStringLiteral("Unknown Check Index setup error"));
    }
}

// Tree node 선택을 BG 완료 전 선택 파싱 또는 완료 DB 범위 선택으로 분기한다.
void CalibreTextDock::OnClickedTreeView(const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    try {
        const std::vector<rdb::CheckId> checkIds =
            tree_model_->GetCheckIds(modelIndex);
        if (!is_parse_complete_) {
            StartSelectionParser(checkIds);
            return;
        }
        if (tree_model_->HasExactResultSelection(modelIndex)) {
            coord_model_->SetActiveResults(
                checkIds,
                tree_model_->GetResultIndices(modelIndex));
        } else {
            coord_model_->SetActiveChecks(checkIds);
        }
        emit UpdateCoordinateTableView();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Tree selection error"));
    }
}

// Check Table 행을 선택하고 필요하면 해당 Check Detail만 별도 worker로 파싱한다.
void CalibreTextDock::OnClickedChipNameTableView(
    const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    try {
        const rdb::CheckId checkId = chip_model_->CheckIdAt(modelIndex.row());
        if (checkId == rdb::invalid_check_id()) return;
        if (!is_parse_complete_) {
            std::vector<rdb::CheckId> checkIds(1U, checkId);
            StartSelectionParser(checkIds);
        } else {
            coord_model_->SetActiveCheck(static_cast<quint64>(checkId));
        }
        emit ChangedCursor(static_cast<quint64>(checkId));
        emit UpdateCoordinateTableView();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Check selection error"));
    }
}

// 화면 행을 실제 Database ResultIndex로 변환해 외부 cursor 신호를 보낸다.
void CalibreTextDock::OnClickedCoordsTableView(
    const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    const rdb::Index resultIndex =
        coord_model_->ResultIndexAt(modelIndex.row());
    if (resultIndex != rdb::invalid_index()) {
        emit ChangedCursor(static_cast<quint64>(resultIndex));
    }
}

// 선택 worker가 보낸 한 배치를 GUI thread의 임시 DetailModel에 적재한다.
void CalibreTextDock::OnUpdateCoordsTable(
    quint64 checkIndex,
    const RDB_DETAIL_BATCH_PTR& batch) {
    try {
        coord_model_->AppendCoords(
            static_cast<rdb::CheckId>(checkIndex), batch);
        emit UpdateCoordinateTableView();
    } catch (const std::exception& error) {
        if (!is_parse_complete_ && selection_parser_) {
            OnSelectionParsingFailed(
                selection_request_id_,
                QString::fromLocal8Bit(error.what()));
        } else {
            OnWorkerFailed(QString::fromLocal8Bit(error.what()));
        }
    } catch (...) {
        if (!is_parse_complete_ && selection_parser_) {
            OnSelectionParsingFailed(
                selection_request_id_,
                QStringLiteral("Unknown detail batch update error"));
        } else {
            OnWorkerFailed(
                QStringLiteral("Unknown detail batch update error"));
        }
    }
}

// 좌표 모델 변경 후 Table viewport의 지연 repaint를 요청한다.
void CalibreTextDock::OnUpdateCoordinateTableView() {
    ui_->coordinate_table_view->viewport()->update();
}

// BG 완성 이후에만 Tree header 위치에 순서형 grouping 메뉴를 표시한다.
void CalibreTextDock::OnCustomContextMenuRequested(const QPoint& position) {
    if (type_ != ALL_PARAMS || !is_parse_complete_) return;
    try {
        InitContextMenu();
        header_menu_->popup(
            ui_->rdb_tree_view->header()->mapToGlobal(position));
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown context menu error"));
    }
}

// 원본 확장 인터페이스를 위한 기본 cursor 변경 hook을 유지한다.
void CalibreTextDock::OnChangedCursor(quint64 index) {
    Q_UNUSED(index)
}

// UI 요청을 예외 경계로 감싸 모든 worker·모델 상태를 초기화한다.
void CalibreTextDock::OnClearAllData() {
    try {
        Clear();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown data clear error"));
    }
}

// 완성 BG Database를 모든 View 모델에 한 번에 연결하고 grouping을 활성화한다.
void CalibreTextDock::OnCompleteBgParsing() {
    if (!completed_database_) return;
    try {
        is_parse_complete_ = true;
        // 완성 DB가 우선이므로 진행 중인 선택 파싱은 즉시 무효화·rollback한다.
        StopSelectionParser();
        chip_model_->SetDatabase(completed_database_);
        coord_model_->SetDatabase(completed_database_);
        bg_model_->SetDatabase(completed_database_);
        tree_model_->SetDatabase(completed_database_);
        SetTreeGroupingEnabled(true);
        InitContextMenu();
        DestroyDetailWorker();
        SelectFirstNavigationItem();
    } catch (const std::exception& error) {
        is_parse_complete_ = false;
        SetTreeGroupingEnabled(false);
        OnWorkerFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        is_parse_complete_ = false;
        SetTreeGroupingEnabled(false);
        OnWorkerFailed(QStringLiteral("Unknown background completion error"));
    }
}

// Key header 클릭을 우클릭과 같은 grouping 메뉴 진입점으로 처리한다.
void CalibreTextDock::OnTreeHeaderClicked(int section) {
    if (section != TREE_VIEW_KEY || type_ != ALL_PARAMS ||
        !is_parse_complete_) return;
    try {
        InitContextMenu();
        header_menu_->popup(QCursor::pos());
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Tree header menu error"));
    }
}

// action에 저장된 key 경로를 정규화해 최대 3 Depth 순서로 Tree를 재빌드한다.
void CalibreTextDock::OnMenuClicked(QAction* action) {
    if (!action || !is_parse_complete_) return;
    QStringList requested = action->data().toStringList();
    // 기존 단일 문자열 action을 직접 전달하는 호출도 계속 허용한다.
    if (requested.isEmpty() && !action->data().toString().isEmpty()) {
        requested.append(action->data().toString());
    }

    // 중복·빈 key를 제거하되 사용자가 선택한 순서는 바꾸지 않는다.
    QStringList keys;
    for (int i = 0; i < requested.size() && keys.size() < 3; ++i) {
        if (!requested[i].isEmpty() && !keys.contains(requested[i])) {
            keys.append(requested[i]);
        }
    }
    if (keys.isEmpty()) {
        keys.append(QStringLiteral("Check Name"));
    }
    try {
        tree_model_->SetCompKeys(keys);
        ui_->rdb_tree_view->expandToDepth(0);
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Tree grouping error"));
    }
}

// 검색어가 바뀌면 목록을 재생성하고 아니면 이전·다음 결과로 순환 이동한다.
void CalibreTextDock::OnSearchNextItem() {
    try {
        const QString requested = ui_->search_edit->text();
        QModelIndex found;
        if (requested != search_text_) {
            search_text_ = requested;
            tree_model_->InitSearch(search_text_);
            found = tree_model_->GetSearchIndex();
        } else {
            const int direction = sender() == ui_->prev_btn ? -1 : 1;
            found = tree_model_->SearchNext(direction);
        }
        if (found.isValid()) {
            ui_->rdb_tree_view->setCurrentIndex(found);
            ui_->rdb_tree_view->scrollTo(found);
        }
        UpdateSearchLabel();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Tree search error"));
    }
}

// 원본 인터페이스를 유지하는 선택 Check 완료 확장 hook이다.
void CalibreTextDock::OnCompleteCheckParsing(quint64 checkIndex) {
    Q_UNUSED(checkIndex)
}

// worker progress를 사용자 callback과 Qt signal 양쪽으로 안전하게 전달한다.
void CalibreTextDock::OnCheckIndexProgress(int value) {
    try {
        if (progress_callback_) progress_callback_(value);
        emit CheckIndexProgress(value);
    } catch (const std::exception& error) {
        OnWorkerFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        OnWorkerFailed(QStringLiteral("Unknown progress callback error"));
    }
}

// Index DB를 즉시 GUI에 표시하고 독립된 전체 Detail BG 파서를 시작한다.
void CalibreTextDock::OnCompleteCheckIndex(
    const RDB_DATABASE_PTR& database) {
    DestroyIndexWorker();
    try {
        is_parse_complete_ = false;
        // 이 시점에는 Check 메타데이터만 있으므로 grouping 메뉴는 계속 차단한다.
        chip_model_->SetDatabase(database);
        coord_model_->SetDatabase(database);
        bg_model_->SetDatabase(database);
        tree_model_->SetDatabase(database);
        SetTreeGroupingEnabled(false);
        StartDetailParser();
        SelectFirstNavigationItem();
    } catch (const std::exception& error) {
        is_parse_complete_ = false;
        SetTreeGroupingEnabled(false);
        OnWorkerFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        is_parse_complete_ = false;
        SetTreeGroupingEnabled(false);
        OnWorkerFailed(QStringLiteral("Unknown Check Index completion error"));
    }
}

// worker가 완성한 DB를 보관하고 GUI 모델 교체 공통 경로로 넘긴다.
void CalibreTextDock::OnCompleteBackgroundDatabase(
    const RDB_DATABASE_PTR& database) {
    completed_database_ = database;
    OnCompleteBgParsing();
}

// 현재 선택 요청의 Check 단위 rollback checkpoint를 GUI 모델에 연다.
void CalibreTextDock::OnStartCheckParsing(
    quint64 requestId,
    quint64 checkIndex) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    try {
        coord_model_->BeginCheckLoad(
            static_cast<rdb::CheckId>(checkIndex));
    } catch (const std::exception& error) {
        OnSelectionParsingFailed(
            requestId, QString::fromLocal8Bit(error.what()));
    } catch (...) {
        OnSelectionParsingFailed(
            requestId, QStringLiteral("Unknown Check load error"));
    }
}

// 현재 requestId의 배치만 모델 갱신 signal로 전달해 오래된 결과를 폐기한다.
void CalibreTextDock::OnSelectedBatchReady(
    quint64 requestId,
    quint64 checkIndex,
    const RDB_DETAIL_BATCH_PTR& batch) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    emit UpdateCoordsTable(checkIndex, batch);
}

// Check header와 누적 Result 수를 검증하고 선택 파싱의 Check 적재를 확정한다.
void CalibreTextDock::OnFinishCheckParsing(
    quint64 requestId,
    quint64 checkIndex,
    const RDB_CHECK_DETAIL_PTR& detail) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    if (!detail) {
        OnSelectionParsingFailed(
            requestId, QStringLiteral("RDB detail metadata is null"));
        return;
    }
    try {
        coord_model_->FinishCheckLoad(
            static_cast<rdb::CheckId>(checkIndex), *detail);
        emit CompleteCheckParsing(checkIndex);
    } catch (const std::exception& error) {
        OnSelectionParsingFailed(
            requestId, QString::fromLocal8Bit(error.what()));
    } catch (...) {
        OnSelectionParsingFailed(
            requestId, QStringLiteral("Unknown Check completion error"));
    }
}

// 현재 선택 요청이 정상 종료되면 worker/thread 소유 객체만 정리한다.
void CalibreTextDock::OnSelectionParsingComplete(quint64 requestId) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    DestroySelectionWorker();
}

// 선택 파싱 실패 시 interrupt·rollback·thread 정리 후 오류를 외부로 전달한다.
void CalibreTextDock::OnSelectionParsingFailed(
    quint64 requestId,
    const QString& message) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    if (selection_interrupt_) selection_interrupt_->store(true);
    coord_model_->CancelCheckLoad();
    DestroySelectionWorker();
    emit ParsingFailed(message);
}

// 사용자 재선택으로 취소된 Check의 부분 배치를 rollback하고 worker를 정리한다.
void CalibreTextDock::OnSelectionParsingCancelled(quint64 requestId) {
    if (requestId != selection_request_id_ || is_parse_complete_) return;
    coord_model_->CancelCheckLoad();
    DestroySelectionWorker();
}

// 공통 worker 오류에서 세대를 무효화하고 모든 파싱 경로를 안전 상태로 되돌린다.
void CalibreTextDock::OnWorkerFailed(const QString& message) {
    // 이미 event queue에 들어온 이전 성공/배치 signal도 이후 generation 검사에서 버린다.
    ++parse_generation_;
    is_parse_complete_ = false;
    SetTreeGroupingEnabled(false);
    if (index_interrupt_) index_interrupt_->store(true);
    if (detail_interrupt_) detail_interrupt_->store(true);
    StopSelectionParser();
    coord_model_->CancelCheckLoad();
    DestroyIndexWorker();
    DestroyDetailWorker();
    emit ParsingFailed(message);
}

// 기존 취소 slot 인터페이스에서 signal 발신 worker 종류에 맞춰 정리한다.
void CalibreTextDock::OnWorkerCancelled() {
    if (sender() == bg_parser_) DestroyIndexWorker();
    if (sender() == detail_parser_) DestroyDetailWorker();
}

// Check Table에 모델·delegate와 읽기 전용 단일 행 선택 정책을 설정한다.
void CalibreTextDock::InitCheckTable() {
    ui_->chip_name_table_view->setModel(chip_model_);
    ui_->chip_name_table_view->setItemDelegate(chip_delegate_);
    ui_->chip_name_table_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->chip_name_table_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->chip_name_table_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->chip_name_table_view->hideColumn(SEEK);
}

// TreeModel과 선택 정책을 연결하고 초기 grouping 메뉴를 비활성화한다.
void CalibreTextDock::InitCheckTree() {
    ui_->rdb_tree_view->setModel(tree_model_);
    ui_->rdb_tree_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->rdb_tree_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->rdb_tree_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->rdb_tree_view->header()->setContextMenuPolicy(Qt::NoContextMenu);
}

// 좌표 Table에 DetailModel을 연결하고 대량 좌표 표시용 단순 정책을 설정한다.
void CalibreTextDock::InitCoordTable() {
    ui_->coordinate_table_view->setModel(coord_model_);
    ui_->coordinate_table_view->setItemDelegate(coord_delegate_);
    ui_->coordinate_table_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->coordinate_table_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->coordinate_table_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->coordinate_table_view->setWordWrap(false);
}

// 사용 가능한 category마다 첫 key 메뉴만 만들고 하위 순열은 열릴 때 생성한다.
void CalibreTextDock::InitContextMenu() {
    header_menu_->clear();
    if (!is_parse_complete_) return;
    const QStringList available = tree_model_->GetAvailableCategories();
    const QStringList selected = tree_model_->GetCompKey();
    // 전체 3단계 순열을 선생성하지 않아 category 수 증가 시 초기 지연을 제한한다.
    for (int i = 0; i < available.size(); ++i) {
        QStringList prefix;
        prefix.append(available[i]);
        QStringList remaining = available;
        remaining.removeAt(i);
        QMenu* firstKeyMenu = header_menu_->addMenu(available[i]);
        PrepareGroupingMenu(
            this,
            firstKeyMenu,
            prefix,
            remaining,
            selected);
    }
}

// View 이벤트와 Dock 내부 갱신 signal을 한 번만 연결한다.
void CalibreTextDock::InitSignalSlot() {
    connect(ui_->rdb_tree_view, &QTreeView::clicked,
            this, &CalibreTextDock::OnClickedTreeView);
    connect(ui_->chip_name_table_view, &QTableView::clicked,
            this, &CalibreTextDock::OnClickedChipNameTableView);
    connect(ui_->coordinate_table_view, &QTableView::clicked,
            this, &CalibreTextDock::OnClickedCoordsTableView);
    connect(ui_->rdb_tree_view->header(), &QHeaderView::sectionClicked,
            this, &CalibreTextDock::OnTreeHeaderClicked);
    connect(ui_->rdb_tree_view->header(),
            &QHeaderView::customContextMenuRequested,
            this, &CalibreTextDock::OnCustomContextMenuRequested);
    connect(ui_->next_btn, &QPushButton::clicked,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(ui_->prev_btn, &QPushButton::clicked,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(ui_->search_edit, &QLineEdit::returnPressed,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(this, &CalibreTextDock::UpdateCoordsTable,
            this, &CalibreTextDock::OnUpdateCoordsTable);
    connect(this, &CalibreTextDock::UpdateCoordinateTableView,
            this, &CalibreTextDock::OnUpdateCoordinateTableView);
    connect(this, &CalibreTextDock::ChangedCursor,
            this, &CalibreTextDock::OnChangedCursor);
    connect(this, &CalibreTextDock::CompleteCheckParsing,
            this, &CalibreTextDock::OnCompleteCheckParsing);
}

// 세 View, splitter 비율, 검색 표시와 Dock 식별자를 초기화한다.
void CalibreTextDock::InitUI() {
    InitCheckTable();
    InitCheckTree();
    InitCoordTable();
    InitContextMenu();
    ui_->splitter->setStretchFactor(0, 1);
    ui_->splitter->setStretchFactor(1, 2);
    ui_->search_index_label->setText(QStringLiteral("0 / 0"));
    SetTreeGroupingEnabled(false);
    setObjectName(QStringLiteral("CalibreTextDock_%1").arg(dock_id_));
}

// parse generation을 올린 뒤 worker·DB·검색·메뉴 상태를 모두 비운다.
void CalibreTextDock::Clear() {
    // generation 증가가 먼저 수행되어 정리 중 도착하는 queued signal도 무효가 된다.
    ++parse_generation_;
    StopWorkers();
    is_parse_complete_ = false;
    completed_database_.reset();
    file_path_.clear();
    parse_header_list_.clear();
    search_text_.clear();
    progress_callback_ = ProgressCallback();
    chip_model_->Clear();
    coord_model_->Clear();
    bg_model_->Clear();
    tree_model_->SetDatabase(RDB_DATABASE_PTR(new rdb::Database));
    SetTreeGroupingEnabled(false);
    ui_->search_edit->clear();
    UpdateSearchLabel();
}

// Index-only DB를 기반으로 처음부터 끝까지 도는 전체 Detail BG thread를 시작한다.
void CalibreTextDock::StartDetailParser() {
    const RDB_DATABASE_PTR database = chip_model_->GetDatabase();
    if (file_path_.isEmpty() || !database ||
        database->rule_checks.empty()) {
        completed_database_ = database;
        OnCompleteBgParsing();
        return;
    }
    detail_interrupt_.reset(new std::atomic<bool>(false));
    detail_thread_ = new QThread;
    // RDBBackgroundParser 내부에서 DB를 다시 복제해 GUI DB와 쓰기 영역을 분리한다.
    detail_parser_ = new RDBBackgroundParser(
        file_path_, database, detail_interrupt_);
    const quint64 generation = parse_generation_;

    connect(detail_thread_, &QThread::started,
            detail_parser_, &RDBBackgroundParser::run);
    connect(detail_thread_, &QThread::finished,
            detail_parser_, &QObject::deleteLater);
    connect(detail_parser_, &RDBBackgroundParser::Complete,
            this, [this, generation](const RDB_DATABASE_PTR& completed) {
                if (generation == parse_generation_) {
                    OnCompleteBackgroundDatabase(completed);
                }
            });
    connect(detail_parser_, &RDBBackgroundParser::ParsingFailed,
            this, [this, generation](const QString& message) {
                if (generation == parse_generation_) {
                    OnWorkerFailed(message);
                }
            });
    connect(detail_parser_, &RDBBackgroundParser::ParsingCancelled,
            this, [this, generation]() {
                if (generation == parse_generation_) {
                    DestroyDetailWorker();
                }
            });
    connect(detail_parser_, &RDBBackgroundParser::Complete,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    connect(detail_parser_, &RDBBackgroundParser::ParsingFailed,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    connect(detail_parser_, &RDBBackgroundParser::ParsingCancelled,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    detail_parser_->moveToThread(detail_thread_);
    detail_thread_->start();
}

// BG 완료 전 선택된 여러 Check만 임시 DB와 별도 thread에서 파싱한다.
void CalibreTextDock::StartSelectionParser(
    const std::vector<rdb::CheckId>& checkIds) {
    if (is_parse_complete_ || file_path_.isEmpty() || checkIds.empty()) return;
    // 새 행 선택은 이전 requestId를 올리고 기존 worker를 interrupt·join한다.
    StopSelectionParser();
    try {
        // 선택용 DB를 분리해 BG worker와 동일 vector에 동시에 쓰지 않게 한다.
        const RDB_DATABASE_PTR selectionDatabase =
            CloneIndexDatabase(chip_model_->GetDatabase());
        coord_model_->SetDatabase(selectionDatabase);
        coord_model_->SetActiveChecks(checkIds);
        selection_interrupt_.reset(new std::atomic<bool>(false));
        selection_thread_ = new QThread;
        selection_parser_ = new RDBDetailParser(
            file_path_, selectionDatabase, checkIds, selection_request_id_,
            selection_interrupt_);

        connect(selection_thread_, &QThread::started,
                selection_parser_, &RDBDetailParser::run);
        connect(selection_thread_, &QThread::finished,
                selection_parser_, &QObject::deleteLater);
        connect(selection_parser_, &RDBDetailParser::CheckParsingStarted,
                this, &CalibreTextDock::OnStartCheckParsing);
        connect(selection_parser_, &RDBDetailParser::BatchReady,
                this, &CalibreTextDock::OnSelectedBatchReady);
        connect(selection_parser_, &RDBDetailParser::CheckParsingComplete,
                this, &CalibreTextDock::OnFinishCheckParsing);
        connect(selection_parser_, &RDBDetailParser::Complete,
                this, &CalibreTextDock::OnSelectionParsingComplete);
        connect(selection_parser_, &RDBDetailParser::ParsingFailed,
                this, &CalibreTextDock::OnSelectionParsingFailed);
        connect(selection_parser_, &RDBDetailParser::ParsingCancelled,
                this, &CalibreTextDock::OnSelectionParsingCancelled);
        connect(selection_parser_, &RDBDetailParser::Complete,
                selection_thread_, &QThread::quit, Qt::DirectConnection);
        connect(selection_parser_, &RDBDetailParser::ParsingFailed,
                selection_thread_, &QThread::quit, Qt::DirectConnection);
        connect(selection_parser_, &RDBDetailParser::ParsingCancelled,
                selection_thread_, &QThread::quit, Qt::DirectConnection);
        selection_parser_->moveToThread(selection_thread_);
        selection_thread_->start();
    } catch (const std::exception& error) {
        StopSelectionParser();
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        StopSelectionParser();
        emit ParsingFailed(QStringLiteral("Unknown Check detail setup error"));
    }
}

// Index·전체 BG·선택 worker에 interrupt를 보낸 뒤 thread 종료를 기다린다.
void CalibreTextDock::StopWorkers() {
    if (index_interrupt_) index_interrupt_->store(true);
    if (detail_interrupt_) detail_interrupt_->store(true);
    StopSelectionParser();
    if (index_thread_) {
        index_thread_->quit();
        index_thread_->wait();
    }
    if (detail_thread_) {
        detail_thread_->quit();
        detail_thread_->wait();
    }
    coord_model_->CancelCheckLoad();
    DestroyIndexWorker();
    DestroyDetailWorker();
}

// 선택 requestId를 즉시 무효화하고 부분 Check 적재를 rollback한다.
void CalibreTextDock::StopSelectionParser() {
    ++selection_request_id_;
    if (selection_interrupt_) selection_interrupt_->store(true);
    if (selection_thread_) {
        selection_thread_->quit();
        selection_thread_->wait();
    }
    coord_model_->CancelCheckLoad();
    DestroySelectionWorker();
}

// Index thread가 끝났음을 보장한 뒤 관련 non-owning worker 포인터를 비운다.
void CalibreTextDock::DestroyIndexWorker() {
    if (index_thread_ && index_thread_->isRunning()) {
        index_thread_->quit();
        index_thread_->wait();
    }
    bg_parser_ = 0;
    delete index_thread_;
    index_thread_ = 0;
    index_interrupt_.reset();
}

// 전체 Detail thread 종료를 보장하고 interrupt 소유권을 해제한다.
void CalibreTextDock::DestroyDetailWorker() {
    if (detail_thread_ && detail_thread_->isRunning()) {
        detail_thread_->quit();
        detail_thread_->wait();
    }
    detail_parser_ = 0;
    delete detail_thread_;
    detail_thread_ = 0;
    detail_interrupt_.reset();
}

// 선택 Detail thread 종료를 보장하고 현재 선택 worker 상태를 비운다.
void CalibreTextDock::DestroySelectionWorker() {
    if (selection_thread_ && selection_thread_->isRunning()) {
        selection_thread_->quit();
        selection_thread_->wait();
    }
    selection_parser_ = 0;
    delete selection_thread_;
    selection_thread_ = 0;
    selection_interrupt_.reset();
}

// BG 완료 여부에 따라 Tree header의 context menu 정책을 원자적으로 전환한다.
void CalibreTextDock::SetTreeGroupingEnabled(bool enabled) {
    ui_->rdb_tree_view->header()->setContextMenuPolicy(
        enabled ? Qt::CustomContextMenu : Qt::NoContextMenu);
    if (!enabled) header_menu_->close();
}

// 현재 모드의 첫 탐색 행을 선택해 초기 Detail 표시 흐름을 재사용한다.
void CalibreTextDock::SelectFirstNavigationItem() {
    if (type_ == COORDS_ONLY) {
        const QModelIndex first = chip_model_->index(0, 0);
        if (first.isValid()) {
            ui_->chip_name_table_view->setCurrentIndex(first);
            OnClickedChipNameTableView(first);
        }
        return;
    }
    const QModelIndex first = tree_model_->index(0, 0);
    if (first.isValid()) {
        ui_->rdb_tree_view->setCurrentIndex(first);
        OnClickedTreeView(first);
    }
}

// 검색 결과의 1 기반 현재 위치와 이동 버튼 활성 상태를 갱신한다.
void CalibreTextDock::UpdateSearchLabel() {
    const int count = tree_model_->SearchCount();
    const int position = tree_model_->SearchPosition();
    ui_->search_index_label->setText(
        QStringLiteral("%1 / %2")
            .arg(position < 0 ? 0 : position + 1).arg(count));
    ui_->next_btn->setEnabled(count > 0);
    ui_->prev_btn->setEnabled(count > 0);
}
