#include "calibre_text_dock_table.hpp"

#include "rdb_check_detail.hpp"
#include "rdb_tree_model.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <atomic>
#include <deque>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

const std::size_t maximumPendingDetailBatches = 2U;

int DetailParserEventType() {
    static const int type = QEvent::registerEventType();
    return type;
}

// 작업 스레드의 결과를 GUI 스레드로 안전하게 전달하는 사용자 이벤트다.
class DetailParserEvent : public QEvent {
public:
    enum Kind {
        CheckStarted,
        Batch,
        CheckFinished,
        AllFinished,
        Failed
    };

    DetailParserEvent(
        unsigned long long requestId,
        rdb::CheckId checkId,
        std::size_t checkNumber,
        std::size_t checkCount,
        const std::shared_ptr<std::atomic<std::size_t> >& pendingBatches,
        std::vector<rdb::DetailResult> results)
        : QEvent(static_cast<QEvent::Type>(DetailParserEventType())),
          kind_(Batch),
          request_id_(requestId),
          check_id_(checkId),
          check_number_(checkNumber),
          check_count_(checkCount),
          results_(std::move(results)),
          pending_batches_(pendingBatches),
          completed_(false) {
        pending_batches_->fetch_add(1U);
    }

    DetailParserEvent(
        Kind kind,
        unsigned long long requestId,
        rdb::CheckId checkId,
        std::size_t checkNumber,
        std::size_t checkCount,
        bool completed,
        const QString& errorMessage = QString(),
        rdb::CheckDetail detail = rdb::CheckDetail())
        : QEvent(static_cast<QEvent::Type>(DetailParserEventType())),
          kind_(kind),
          request_id_(requestId),
          check_id_(checkId),
          check_number_(checkNumber),
          check_count_(checkCount),
          error_message_(errorMessage),
          detail_(std::move(detail)),
          pending_batches_(),
          completed_(completed) {}

    ~DetailParserEvent() override {
        if (pending_batches_) pending_batches_->fetch_sub(1U);
    }

    Kind kind_;
    unsigned long long request_id_;
    rdb::CheckId check_id_;
    std::size_t check_number_;
    std::size_t check_count_;
    std::vector<rdb::DetailResult> results_;
    QString error_message_;
    rdb::CheckDetail detail_;
    std::shared_ptr<std::atomic<std::size_t> > pending_batches_;
    bool completed_;
};

struct DetailWorkItem {
    rdb::CheckId check_id_;
    rdb::CheckOffset offset_;

    DetailWorkItem(rdb::CheckId id, rdb::CheckOffset checkOffset)
        : check_id_(id), offset_(checkOffset) {}
};

QString TextFromDatabase(
    const rdb::Database& database,
    rdb::StringId id) {
    const rdb::StringRef text = database.strings.get(id);
    return text.data
        ? QString::fromUtf8(text.data, static_cast<int>(text.size))
        : QString();
}

} // namespace

struct CalibreTextDockTable::ParserTask {
    std::shared_ptr<std::atomic<bool> > cancelled_;
    std::shared_ptr<std::atomic<bool> > finished_;
    std::thread thread_;
};

CalibreTextDockTable::CalibreTextDockTable(QWidget* parent)
    : QDockWidget(tr("CalibreTextDockTable"), parent),
      check_stack_(0),
      check_table_(0),
      all_params_page_(0),
      check_tree_view_(0),
      tree_search_edit_(0),
      tree_search_button_(0),
      tree_search_previous_button_(0),
      tree_search_next_button_(0),
      coords_table_(0),
      check_model_(new RdbTableModel(RdbTableModel::CheckIndex, this)),
      detail_model_(new RdbTableModel(
          RdbTableModel::CoordinatesOnly, this)),
      tree_model_(new RdbTreeModel(this)),
      detail_type_(RdbTableModel::CoordinatesOnly),
      active_request_id_(0),
      tree_search_position_(0U),
      selection_restart_enabled_(false),
      tree_grouping_menu_enabled_(false) {
    setObjectName(QStringLiteral("CalibreTextDockTable"));

    QSplitter* dockSplitter = new QSplitter(Qt::Horizontal, this);
    dockSplitter->setObjectName(QStringLiteral("CalibreTextDockSplitter"));

    check_stack_ = new QStackedWidget(dockSplitter);
    check_stack_->setObjectName(QStringLiteral("CheckViewStack"));

    check_table_ = new QTableView(check_stack_);
    check_table_->setObjectName(QStringLiteral("CheckTable"));
    check_table_->setModel(check_model_);
    check_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    check_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    check_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    check_table_->setSortingEnabled(false);
    check_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    check_table_->verticalHeader()->setMinimumSectionSize(42);
    check_table_->verticalHeader()->setDefaultSectionSize(42);

    check_stack_->addWidget(check_table_);

    // All Params 전용 페이지는 TreeView와 검색 도구를 세로로 배치한다.
    all_params_page_ = new QWidget(check_stack_);
    all_params_page_->setObjectName(QStringLiteral("AllParamsPage"));
    QVBoxLayout* const allParamsLayout = new QVBoxLayout(all_params_page_);
    allParamsLayout->setContentsMargins(0, 0, 0, 0);
    allParamsLayout->setSpacing(4);

    check_tree_view_ = new QTreeView(all_params_page_);
    check_tree_view_->setObjectName(QStringLiteral("CheckTreeView"));
    check_tree_view_->setModel(tree_model_);
    check_tree_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    check_tree_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    check_tree_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    check_tree_view_->header()->setContextMenuPolicy(Qt::NoContextMenu);
    allParamsLayout->addWidget(check_tree_view_, 1);

    QWidget* const searchBar = new QWidget(all_params_page_);
    searchBar->setObjectName(QStringLiteral("TreeSearchBar"));
    QHBoxLayout* const searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(4);

    tree_search_edit_ = new QLineEdit(searchBar);
    tree_search_edit_->setObjectName(QStringLiteral("TreeSearchEdit"));
    tree_search_edit_->setPlaceholderText(tr("Search tree..."));
    tree_search_button_ = new QPushButton(tr("Search"), searchBar);
    tree_search_button_->setObjectName(QStringLiteral("TreeSearchButton"));
    tree_search_previous_button_ = new QPushButton(tr("<"), searchBar);
    tree_search_previous_button_->setObjectName(
        QStringLiteral("TreeSearchPreviousButton"));
    tree_search_previous_button_->setFixedWidth(32);
    tree_search_next_button_ = new QPushButton(tr(">"), searchBar);
    tree_search_next_button_->setObjectName(
        QStringLiteral("TreeSearchNextButton"));
    tree_search_next_button_->setFixedWidth(32);
    tree_search_previous_button_->setEnabled(false);
    tree_search_next_button_->setEnabled(false);

    searchLayout->addWidget(tree_search_edit_, 1);
    searchLayout->addWidget(tree_search_button_);
    searchLayout->addWidget(tree_search_previous_button_);
    searchLayout->addWidget(tree_search_next_button_);
    allParamsLayout->addWidget(searchBar);

    check_stack_->addWidget(all_params_page_);
    check_stack_->setCurrentWidget(check_table_);

    coords_table_ = new QTableView(dockSplitter);
    coords_table_->setObjectName(QStringLiteral("CoordsTable"));
    coords_table_->setModel(detail_model_);
    coords_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    coords_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    coords_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    coords_table_->setWordWrap(false);
    coords_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    coords_table_->verticalHeader()->setMinimumSectionSize(42);
    coords_table_->verticalHeader()->setDefaultSectionSize(42);

    dockSplitter->addWidget(check_stack_);
    dockSplitter->addWidget(coords_table_);
    dockSplitter->setStretchFactor(0, 1);
    dockSplitter->setStretchFactor(1, 2);
    dockSplitter->setSizes(QList<int>() << 320 << 680);

    setWidget(dockSplitter);

    connect(
        check_table_->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this](const QModelIndex& current, const QModelIndex&) {
            OnCheckRowSelected(current);
        });
    connect(
        check_tree_view_->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        [this](const QModelIndex& current, const QModelIndex&) {
            OnCheckTreeSelected(current);
        });
    connect(
        check_tree_view_->header(),
        &QHeaderView::customContextMenuRequested,
        this,
        [this](const QPoint& position) {
            ShowTreeGroupingMenu(position);
        });
    connect(
        tree_search_button_,
        &QPushButton::clicked,
        this,
        [this]() { SearchTree(); });
    connect(
        tree_search_edit_,
        &QLineEdit::returnPressed,
        this,
        [this]() { SearchTree(); });
    connect(
        tree_search_edit_,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) { ClearTreeSearchResults(); });
    connect(
        tree_search_previous_button_,
        &QPushButton::clicked,
        this,
        [this]() { MoveTreeSearch(-1); });
    connect(
        tree_search_next_button_,
        &QPushButton::clicked,
        this,
        [this]() { MoveTreeSearch(1); });
    connect(
        tree_model_,
        &QAbstractItemModel::modelReset,
        this,
        [this]() { ClearTreeSearchResults(); });

    ConfigureDetailHeader();
}

CalibreTextDockTable::~CalibreTextDockTable() {
    CancelDetailParsers();
    for (std::size_t i = 0; i < parser_tasks_.size(); ++i) {
        if (parser_tasks_[i]->thread_.joinable()) {
            parser_tasks_[i]->thread_.join();
        }
    }
    QCoreApplication::removePostedEvents(this, DetailParserEventType());
}

void CalibreTextDockTable::LoadRdbIndex(
    const QString& path,
    bool allParameters) {
    QMainWindow* const mainWindow =
        qobject_cast<QMainWindow*>(parentWidget());
    ShowStatusMessage(tr("Reading RDB check index..."));
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    try {
        const std::size_t checkCount = LoadRdbIndex(
            path,
            allParameters
                ? RdbTableModel::AllParameters
                : RdbTableModel::CoordinatesOnly,
            [this](int progress) {
                ShowStatusMessage(
                    tr("Reading RDB check index... %1%").arg(progress));
                QCoreApplication::processEvents(
                    QEventLoop::ExcludeUserInputEvents);
            });

        if (mainWindow) {
            mainWindow->setWindowTitle(
                tr("MainWindow App - %1")
                    .arg(QFileInfo(path).fileName()));
        }
        ShowStatusMessage(
            tr("%1 checks loaded").arg(
                static_cast<qulonglong>(checkCount)));
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& error) {
        QApplication::restoreOverrideCursor();
        ShowStatusMessage(tr("Failed to read RDB check index"));
        QMessageBox::critical(
            mainWindow ? static_cast<QWidget*>(mainWindow) : this,
            tr("RDB Index Error"),
            QString::fromLocal8Bit(error.what()));
    }
}

std::size_t CalibreTextDockTable::LoadRdbIndex(
    const QString& path,
    RdbTableModel::ModelType detailType,
    const ProgressCallback& progressCallback) {
    if (detailType == RdbTableModel::CheckIndex) {
        throw std::invalid_argument("Invalid RDB detail table type");
    }

    CancelDetailParsers();
    ++active_request_id_;
    ReapFinishedParserTasks();
    detail_model_->CancelDetailLoad();
    selection_restart_enabled_ = false;
    EnableTreeGroupingMenu(false);
    ClearTreeSearchResults();

    const QFileInfo sourceInfo(path);
    QString normalizedPath = sourceInfo.canonicalFilePath();
    if (normalizedPath.isEmpty()) normalizedPath = sourceInfo.absoluteFilePath();
    const QString fileIdentity = QStringLiteral("%1\n%2\n%3")
        .arg(normalizedPath)
        .arg(sourceInfo.size())
        .arg(sourceInfo.lastModified().toMSecsSinceEpoch());
    detail_type_ = detailType;
    check_table_->setEnabled(false);
    check_tree_view_->setEnabled(false);

    const bool reuseDatabase =
        source_file_identity_ == fileIdentity &&
        check_model_->GetDatabase() &&
        check_model_->GetDatabase()->check_count() != 0U;
    if (reuseDatabase) {
        detail_model_->SetDatabase(detail_type_, check_model_->GetDatabase());
        if (progressCallback) progressCallback(100);
    } else {
        source_path_.clear();
        rdb::FastCheckIndexOptions options;
        options.progress_callback = progressCallback;

        try {
            const QByteArray encodedPath = QFile::encodeName(path);
            rdb::CheckIndexDatabase index =
                rdb::FastCheckIndexParser().parse_database(
                    encodedPath.constData(), options);
            check_model_->SetCheckIndexDatabase(std::move(index));
            detail_model_->SetDatabase(
                detail_type_, check_model_->GetDatabase());
        } catch (...) {
            check_table_->setEnabled(true);
            check_tree_view_->setEnabled(true);
            throw;
        }
    }

    if (detail_type_ == RdbTableModel::AllParameters) {
        tree_model_->SetDatabase(check_model_->GetDatabase());
    }

    source_path_ = path;
    source_file_identity_ = fileIdentity;
    check_table_->clearSelection();
    check_table_->setCurrentIndex(QModelIndex());
    check_tree_view_->clearSelection();
    check_tree_view_->setCurrentIndex(QModelIndex());
    check_table_->setEnabled(true);
    check_tree_view_->setEnabled(true);

    ConfigureCheckView();
    SelectFirstCheck();
    StartBackgroundDetailParser(detail_model_->GetSelectedCheckIds());
    selection_restart_enabled_ = true;
    if (check_model_->GetDatabase()->loaded_rule_check_count ==
        check_model_->GetDatabase()->check_count()) {
        EnableTreeGroupingMenu(
            detail_type_ == RdbTableModel::AllParameters);
    }

    return check_model_->TotalRowCount();
}

bool CalibreTextDockTable::event(QEvent* eventObject) {
    if (eventObject->type() != DetailParserEventType()) {
        return QDockWidget::event(eventObject);
    }

    DetailParserEvent* parserEvent =
        static_cast<DetailParserEvent*>(eventObject);
    if (parserEvent->request_id_ != active_request_id_) {
        ReapFinishedParserTasks();
        return true;
    }

    if (parserEvent->kind_ == DetailParserEvent::CheckStarted) {
        try {
            detail_model_->BeginDetailLoad(parserEvent->check_id_);
        } catch (const std::exception& error) {
            CancelDetailParsers();
            ++active_request_id_;
            detail_model_->CancelDetailLoad();
            ShowStatusMessage(tr("Failed to start background RDB detail parsing"));
            ShowDetailError(QString::fromLocal8Bit(error.what()));
            ReapFinishedParserTasks();
            return true;
        }

        const rdb::Database& database = *check_model_->GetDatabase();
        const QString checkName = TextFromDatabase(
            database, database.check(parserEvent->check_id_).name);
        ShowStatusMessage(
            tr("Background detail parsing %1/%2: %3")
                .arg(static_cast<qulonglong>(parserEvent->check_number_))
                .arg(static_cast<qulonglong>(parserEvent->check_count_))
                .arg(checkName));
        return true;
    }

    if (parserEvent->kind_ == DetailParserEvent::Batch) {
        try {
            if (!detail_model_->IsDetailLoadActive() ||
                detail_model_->LoadingCheckId() != parserEvent->check_id_) {
                throw std::logic_error(
                    "RDB detail batch does not match the active Check");
            }
            detail_model_->AppendDetailResults(
                std::move(parserEvent->results_));
        } catch (const std::exception& error) {
            CancelDetailParsers();
            ++active_request_id_;
            detail_model_->CancelDetailLoad();
            ShowStatusMessage(tr("Failed to store RDB detail results"));
            ShowDetailError(QString::fromLocal8Bit(error.what()));
            ReapFinishedParserTasks();
            return true;
        }
        if (detail_model_->IsCheckSelected(parserEvent->check_id_)) {
            ShowStatusMessage(
                tr("Background detail parsing %1/%2: %3 rows")
                    .arg(static_cast<qulonglong>(parserEvent->check_number_))
                    .arg(static_cast<qulonglong>(parserEvent->check_count_))
                    .arg(static_cast<qulonglong>(
                        detail_model_->TotalRowCount())));
        }
        return true;
    }

    if (parserEvent->kind_ == DetailParserEvent::Failed) {
        CancelDetailParsers();
        ++active_request_id_;
        detail_model_->CancelDetailLoad();
        ShowStatusMessage(tr("Failed to parse RDB details in background"));
        ShowDetailError(parserEvent->error_message_);
        ReapFinishedParserTasks();
        return true;
    }

    if (parserEvent->kind_ == DetailParserEvent::AllFinished) {
        if (detail_type_ == RdbTableModel::AllParameters) {
            selection_restart_enabled_ = false;
            tree_model_->Rebuild();
            EnableTreeGroupingMenu(true);
            SelectFirstCheck();
            selection_restart_enabled_ = true;
        }
        ShowStatusMessage(
            tr("All %1 RDB checks were parsed in background")
                .arg(static_cast<qulonglong>(
                    check_model_->GetDatabase()->loaded_rule_check_count)));
        ReapFinishedParserTasks();
        return true;
    }

    try {
        if (parserEvent->completed_) {
            detail_model_->FinishDetailLoad(parserEvent->detail_);
        } else {
            detail_model_->CancelDetailLoad();
        }
    } catch (const std::exception& error) {
        CancelDetailParsers();
        ++active_request_id_;
        detail_model_->CancelDetailLoad();
        ShowStatusMessage(tr("Failed to finalize RDB detail results"));
        ShowDetailError(QString::fromLocal8Bit(error.what()));
        ReapFinishedParserTasks();
        return true;
    }
    ShowStatusMessage(
        parserEvent->completed_
            ? tr("Background detail parsing %1/%2 complete")
                  .arg(static_cast<qulonglong>(parserEvent->check_number_))
                  .arg(static_cast<qulonglong>(parserEvent->check_count_))
            : tr("RDB detail parsing interrupted"));
    ReapFinishedParserTasks();
    return true;
}

void CalibreTextDockTable::OnCheckRowSelected(
    const QModelIndex& current) {
    ReapFinishedParserTasks();

    if (!current.isValid() || source_path_.isEmpty()) {
        detail_model_->SelectDetailCheck(rdb::invalid_check_id());
        ConfigureDetailHeader();
        return;
    }
    const rdb::CheckId checkId = check_model_->CheckIdAt(current.row());
    if (checkId == rdb::invalid_check_id()) {
        detail_model_->SelectDetailCheck(rdb::invalid_check_id());
        ConfigureDetailHeader();
        return;
    }

    detail_model_->SelectDetailCheck(checkId);
    ConfigureDetailHeader();

    const rdb::RuleCheck& check = check_model_->GetDatabase()->check(checkId);
    if (check.detail_loaded) {
        ShowStatusMessage(
            tr("%1 detail results restored from cache")
                .arg(static_cast<qulonglong>(
                    detail_model_->TotalRowCount())));
        return;
    }

    if (detail_model_->IsDetailLoadActive() &&
        detail_model_->LoadingCheckId() == checkId) {
        ShowStatusMessage(
            tr("This Check is being parsed in background... %1 rows")
                .arg(static_cast<qulonglong>(
                    detail_model_->TotalRowCount())));
    } else {
        ShowStatusMessage(
            tr("Waiting for this Check's background detail parsing"));
    }
}

void CalibreTextDockTable::OnCheckTreeSelected(
    const QModelIndex& current) {
    ReapFinishedParserTasks();

    if (!current.isValid() || source_path_.isEmpty()) {
        detail_model_->SelectDetailChecks(std::vector<rdb::CheckId>());
        ConfigureDetailHeader();
        return;
    }

    const std::vector<rdb::CheckId> checkIds =
        tree_model_->GetCheckIds(current);
    if (tree_model_->HasExactResultSelection(current)) {
        detail_model_->SelectDetailResults(
            checkIds, tree_model_->GetResultIndices(current));
    } else {
        detail_model_->SelectDetailChecks(checkIds);
    }
    ConfigureDetailHeader();

    if (detail_model_->IsSelectedDetailLoaded()) {
        ShowStatusMessage(
            tr("%1 grouped detail results restored from cache")
                .arg(static_cast<qulonglong>(
                    detail_model_->TotalRowCount())));
        return;
    }

    ShowStatusMessage(
        tr("Parsing %1 Checks selected in the tree")
            .arg(static_cast<qulonglong>(checkIds.size())));
    if (selection_restart_enabled_) {
        RestartBackgroundDetailParserForSelection();
    }
}

void CalibreTextDockTable::RestartBackgroundDetailParserForSelection() {
    const std::vector<rdb::CheckId>& selected =
        detail_model_->GetSelectedCheckIds();
    if (selected.empty() || detail_model_->IsSelectedDetailLoaded()) return;
    if (detail_model_->IsDetailLoadActive() &&
        detail_model_->IsCheckSelected(detail_model_->LoadingCheckId())) {
        return;
    }

    // 선택된 Tree 노드를 우선 처리하도록 기존 작업을 중단하고 순서를 재구성한다.
    CancelDetailParsers();
    ++active_request_id_;
    detail_model_->CancelDetailLoad();
    ReapFinishedParserTasks();
    StartBackgroundDetailParser(selected);
}

void CalibreTextDockTable::StartBackgroundDetailParser(
    const std::vector<rdb::CheckId>& priorityCheckIds) {
    // 아직 캐시되지 않은 Check만 작업 목록에 넣어 한 개의 스레드에서 순차 처리한다.
    const unsigned long long requestId = active_request_id_;
    const std::string path = QFile::encodeName(source_path_).constData();
    const rdb::Database& database = *check_model_->GetDatabase();
    std::vector<DetailWorkItem> work;
    work.reserve(database.rule_checks.size());
    std::vector<bool> queued(database.rule_checks.size(), false);
    for (std::size_t i = 0; i < priorityCheckIds.size(); ++i) {
        const std::size_t id = static_cast<std::size_t>(priorityCheckIds[i]);
        if (id < database.rule_checks.size() &&
            !database.rule_checks[id].detail_loaded && !queued[id]) {
            work.push_back(DetailWorkItem(
                priorityCheckIds[i], database.rule_checks[id].offset));
            queued[id] = true;
        }
    }
    for (std::size_t i = 0; i < database.rule_checks.size(); ++i) {
        const rdb::RuleCheck& check = database.rule_checks[i];
        if (!check.detail_loaded && !queued[i]) {
            work.push_back(DetailWorkItem(
                static_cast<rdb::CheckId>(i), check.offset));
            queued[i] = true;
        }
    }

    if (work.empty()) {
        ShowStatusMessage(tr("All RDB details are already cached"));
        return;
    }

    std::unique_ptr<ParserTask> task(new ParserTask);
    task->cancelled_.reset(new std::atomic<bool>(false));
    task->finished_.reset(new std::atomic<bool>(false));
    const std::shared_ptr<std::atomic<bool> > cancelled = task->cancelled_;
    const std::shared_ptr<std::atomic<bool> > finished = task->finished_;
    const std::shared_ptr<std::atomic<std::size_t> > pendingBatches(
        new std::atomic<std::size_t>(0U));

    ShowStatusMessage(
        tr("Background detail parsing started for %1 checks")
            .arg(static_cast<qulonglong>(work.size())));

    task->thread_ = std::thread(
        [this, requestId, path, work, cancelled, finished, pendingBatches]() {
            rdb::CheckId currentCheckId = rdb::invalid_check_id();
            std::size_t currentCheckNumber = 0U;
            const std::size_t totalCheckCount = work.size();
            try {
                rdb::CheckDetailFile detailFile(path);
                for (std::size_t i = 0; i < work.size(); ++i) {
                    if (cancelled->load()) break;
                    currentCheckId = work[i].check_id_;
                    currentCheckNumber = i + 1U;

                    QCoreApplication::postEvent(
                        this,
                        new DetailParserEvent(
                            DetailParserEvent::CheckStarted,
                            requestId,
                            currentCheckId,
                            currentCheckNumber,
                            totalCheckCount,
                            false));

                    rdb::CheckDetailBatchOptions options;
                    options.batch_size = 10000U;
                    options.is_cancelled = [cancelled]() {
                        return cancelled->load();
                    };
                    options.batch_callback =
                        [this, requestId, currentCheckId,
                         currentCheckNumber, totalCheckCount,
                         cancelled, pendingBatches](
                            const std::vector<rdb::DetailResult>& batch) {
                            while (pendingBatches->load() >=
                                       maximumPendingDetailBatches &&
                                   !cancelled->load()) {
                                std::this_thread::yield();
                            }
                            if (cancelled->load()) return;
                            std::vector<rdb::DetailResult> results(
                                batch.begin(), batch.end());
                            if (!cancelled->load()) {
                                QCoreApplication::postEvent(
                                    this,
                                    new DetailParserEvent(
                                        requestId,
                                        currentCheckId,
                                        currentCheckNumber,
                                        totalCheckCount,
                                        pendingBatches,
                                        std::move(results)));
                            }
                        };

                    rdb::CheckDetailBatchResult result =
                        detailFile.parse_at_batches(
                            work[i].offset_, options);
                    if (cancelled->load() || !result.completed) break;

                    QCoreApplication::postEvent(
                        this,
                        new DetailParserEvent(
                            DetailParserEvent::CheckFinished,
                            requestId,
                            currentCheckId,
                            currentCheckNumber,
                            totalCheckCount,
                            true,
                            QString(),
                            std::move(result.detail)));
                }

                if (cancelled->load()) {
                    finished->store(true);
                    return;
                }

                finished->store(true);
                QCoreApplication::postEvent(
                    this,
                    new DetailParserEvent(
                        DetailParserEvent::AllFinished,
                        requestId,
                        rdb::invalid_check_id(),
                        totalCheckCount,
                        totalCheckCount,
                        true));
            } catch (const std::exception& error) {
                finished->store(true);
                if (!cancelled->load()) {
                    QCoreApplication::postEvent(
                        this,
                        new DetailParserEvent(
                            DetailParserEvent::Failed,
                            requestId,
                            currentCheckId,
                            currentCheckNumber,
                            totalCheckCount,
                            false,
                            QString::fromLocal8Bit(error.what())));
                }
            } catch (...) {
                finished->store(true);
                if (!cancelled->load()) {
                    QCoreApplication::postEvent(
                        this,
                        new DetailParserEvent(
                            DetailParserEvent::Failed,
                            requestId,
                            currentCheckId,
                            currentCheckNumber,
                            totalCheckCount,
                            false,
                            QStringLiteral("Unknown parser error")));
                }
            }
        });

    parser_tasks_.push_back(std::move(task));
}

void CalibreTextDockTable::CancelDetailParsers() {
    for (std::size_t i = 0; i < parser_tasks_.size(); ++i) {
        parser_tasks_[i]->cancelled_->store(true);
    }
}

void CalibreTextDockTable::ReapFinishedParserTasks() {
    std::vector<std::unique_ptr<ParserTask> >::iterator task =
        parser_tasks_.begin();
    while (task != parser_tasks_.end()) {
        if ((*task)->finished_->load()) {
            if ((*task)->thread_.joinable()) (*task)->thread_.join();
            task = parser_tasks_.erase(task);
        } else {
            ++task;
        }
    }
}

void CalibreTextDockTable::ConfigureCheckView() {
    if (detail_type_ == RdbTableModel::AllParameters) {
        check_stack_->setCurrentWidget(all_params_page_);
        check_tree_view_->header()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        check_tree_view_->header()->setSectionResizeMode(
            1, QHeaderView::ResizeToContents);
        check_tree_view_->expandToDepth(0);
        return;
    }

    check_stack_->setCurrentWidget(check_table_);
    check_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    check_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
}

void CalibreTextDockTable::SelectFirstCheck() {
    if (detail_type_ == RdbTableModel::AllParameters) {
        if (tree_model_->rowCount() == 0) return;
        const QModelIndex first = tree_model_->index(0, 0);
        check_tree_view_->selectionModel()->setCurrentIndex(
            first,
            QItemSelectionModel::ClearAndSelect |
                QItemSelectionModel::Rows);
        check_tree_view_->scrollTo(first);
        return;
    }

    if (check_model_->rowCount() == 0) return;
    const QModelIndex first = check_model_->index(0, 0);
    check_table_->selectionModel()->setCurrentIndex(
        first,
        QItemSelectionModel::ClearAndSelect |
            QItemSelectionModel::Rows);
    check_table_->scrollTo(first);
}

void CalibreTextDockTable::EnableTreeGroupingMenu(bool enabled) {
    tree_grouping_menu_enabled_ = enabled;
    check_tree_view_->header()->setContextMenuPolicy(
        enabled ? Qt::CustomContextMenu : Qt::NoContextMenu);
    check_tree_view_->header()->setProperty("GroupingReady", enabled);
}

void CalibreTextDockTable::ShowTreeGroupingMenu(const QPoint& position) {
    if (!tree_grouping_menu_enabled_) return;

    QMenu menu(check_tree_view_);
    QAction* const reset = menu.addAction(tr("Check Name으로 초기화"));
    connect(reset, &QAction::triggered, this, [this]() {
        tree_model_->SetGroupingCategories(
            QStringList() << RdbTreeModel::CheckNameCategory());
        check_tree_view_->expandToDepth(0);
        SelectFirstCheck();
    });
    menu.addSeparator();

    const QStringList current = tree_model_->GetGroupingCategories();
    const QStringList available = tree_model_->GetAvailableCategories();
    for (int depth = 0; depth < 3; ++depth) {
        QMenu* const depthMenu = menu.addMenu(
            tr("그룹 Depth %1").arg(depth + 1));
        depthMenu->setEnabled(depth == 0 || current.size() >= depth);

        if (depth > 0 && current.size() > depth) {
            QAction* const removeDepth =
                depthMenu->addAction(tr("이 Depth부터 제거"));
            connect(removeDepth, &QAction::triggered, this, [this, depth]() {
                ApplyTreeGrouping(depth, QString());
            });
            depthMenu->addSeparator();
        }

        for (int categoryIndex = 0;
             categoryIndex < available.size();
             ++categoryIndex) {
            const QString category = available[categoryIndex];
            QAction* const action = depthMenu->addAction(category);
            action->setCheckable(true);
            action->setChecked(
                current.size() > depth && current[depth] == category);
            const int usedDepth = current.indexOf(category);
            if (usedDepth >= 0 && usedDepth != depth) action->setEnabled(false);
            connect(
                action,
                &QAction::triggered,
                this,
                [this, depth, category]() {
                    ApplyTreeGrouping(depth, category);
                });
        }
    }

    menu.exec(check_tree_view_->header()->mapToGlobal(position));
}

void CalibreTextDockTable::ApplyTreeGrouping(
    int depth,
    const QString& category) {
    if (depth < 0 || depth >= 3) return;
    QStringList categories = tree_model_->GetGroupingCategories();
    if (category.isEmpty()) {
        while (categories.size() > depth) categories.removeLast();
    } else {
        if (depth > categories.size()) return;
        while (categories.size() > depth) categories.removeLast();
        categories << category;
    }
    if (categories.isEmpty()) categories << RdbTreeModel::CheckNameCategory();

    selection_restart_enabled_ = false;
    tree_model_->SetGroupingCategories(categories);
    check_tree_view_->expandToDepth(categories.size() - 1);
    SelectFirstCheck();
    selection_restart_enabled_ = true;
}

void CalibreTextDockTable::SearchTree() {
    ClearTreeSearchResults();
    tree_search_text_ = tree_search_edit_->text();
    if (tree_search_text_.isEmpty()) {
        ShowStatusMessage(tr("Enter a tree search string"));
        return;
    }

    // 같은 깊이의 노드를 먼저 검사하도록 큐를 사용해 BFS 순회한다.
    std::deque<QModelIndex> nodes;
    const int rootRows = tree_model_->rowCount();
    for (int row = 0; row < rootRows; ++row) {
        nodes.push_back(tree_model_->index(row, 0));
    }

    while (!nodes.empty()) {
        const QModelIndex current = nodes.front();
        nodes.pop_front();
        if (tree_model_->data(current, Qt::DisplayRole)
                .toString()
                .contains(tree_search_text_, Qt::CaseInsensitive)) {
            tree_search_results_.push_back(QPersistentModelIndex(current));
        }

        const int childRows = tree_model_->rowCount(current);
        for (int row = 0; row < childRows; ++row) {
            nodes.push_back(tree_model_->index(row, 0, current));
        }
    }

    const bool found = !tree_search_results_.empty();
    tree_search_previous_button_->setEnabled(found);
    tree_search_next_button_->setEnabled(found);
    tree_search_position_ = 0U;
    if (found) SelectTreeSearchResult();
    ShowStatusMessage(
        found
            ? tr("%1 tree search result(s)")
                  .arg(static_cast<qulonglong>(tree_search_results_.size()))
            : tr("No tree search results"));
}

void CalibreTextDockTable::MoveTreeSearch(int direction) {
    if (tree_search_results_.empty() ||
        tree_search_text_ != tree_search_edit_->text()) {
        SearchTree();
        return;
    }

    const std::size_t count = tree_search_results_.size();
    if (direction < 0) {
        tree_search_position_ =
            tree_search_position_ == 0U
                ? count - 1U
                : tree_search_position_ - 1U;
    } else {
        tree_search_position_ = (tree_search_position_ + 1U) % count;
    }
    SelectTreeSearchResult();
    ShowStatusMessage(
        tr("Tree search result %1/%2")
            .arg(static_cast<qulonglong>(tree_search_position_ + 1U))
            .arg(static_cast<qulonglong>(count)));
}

void CalibreTextDockTable::ClearTreeSearchResults() {
    tree_search_results_.clear();
    tree_search_position_ = 0U;
    tree_search_text_.clear();
    if (tree_search_previous_button_) {
        tree_search_previous_button_->setEnabled(false);
    }
    if (tree_search_next_button_) tree_search_next_button_->setEnabled(false);
}

void CalibreTextDockTable::SelectTreeSearchResult() {
    if (tree_search_position_ >= tree_search_results_.size()) return;
    const QModelIndex result = tree_search_results_[tree_search_position_];
    if (!result.isValid()) {
        ClearTreeSearchResults();
        return;
    }

    // 검색된 하위 노드가 보이도록 모든 부모를 펼친 후 행 전체를 선택한다.
    QModelIndex parent = result.parent();
    while (parent.isValid()) {
        check_tree_view_->expand(parent);
        parent = parent.parent();
    }
    check_tree_view_->selectionModel()->setCurrentIndex(
        result,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    check_tree_view_->scrollTo(result, QAbstractItemView::PositionAtCenter);
}

void CalibreTextDockTable::ConfigureDetailHeader() {
    coords_table_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
}

void CalibreTextDockTable::ShowStatusMessage(const QString& message) {
    QMainWindow* mainWindow = qobject_cast<QMainWindow*>(parentWidget());
    if (mainWindow) mainWindow->statusBar()->showMessage(message);
}

void CalibreTextDockTable::ShowDetailError(const QString& message) {
    if (isVisible()) {
        QMessageBox::critical(
            this, tr("RDB Detail Error"), message);
    } else {
        qWarning().noquote() << tr("RDB Detail Error:") << message;
    }
}
