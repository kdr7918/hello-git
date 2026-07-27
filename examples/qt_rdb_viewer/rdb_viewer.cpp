#include "rdb_viewer.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

struct RdbViewerAsyncState {
    RdbViewerAsyncState() : activeTasks(0) {}

    void start() {
        std::lock_guard<std::mutex> lock(mutex);
        ++activeTasks;
    }
    void finish() {
        std::lock_guard<std::mutex> lock(mutex);
        if (--activeTasks == 0) condition.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        while (activeTasks != 0) condition.wait(lock);
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::size_t activeTasks;
};

namespace {

class AsyncTaskGuard {
public:
    explicit AsyncTaskGuard(const std::shared_ptr<RdbViewerAsyncState>& state) : state_(state) {}
    ~AsyncTaskGuard() { state_->finish(); }
private:
    std::shared_ptr<RdbViewerAsyncState> state_;
};

const QEvent::Type indexProgressEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type indexReadyEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type geometryReadyEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type databaseReadyEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type detailRowsEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type detailCompleteEventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());

class IndexProgressEvent : public QEvent {
public:
    IndexProgressEvent(quint64 generationValue, int progressValue)
        : QEvent(indexProgressEventType), generation(generationValue), progress(progressValue) {}
    quint64 generation;
    int progress;
};

class IndexReadyEvent : public QEvent {
public:
    IndexReadyEvent(quint64 generationValue,
                    const std::shared_ptr<rdb::CheckIndexDatabase>& value,
                    const std::exception_ptr& exception)
        : QEvent(indexReadyEventType), generation(generationValue), index(value), error(exception) {}
    quint64 generation;
    std::shared_ptr<rdb::CheckIndexDatabase> index;
    std::exception_ptr error;
};

class GeometryReadyEvent : public QEvent {
public:
    GeometryReadyEvent(quint64 generationValue,
                       const std::shared_ptr<rdb::GeometryDatabase>& value,
                       const std::exception_ptr& exception)
        : QEvent(geometryReadyEventType), generation(generationValue), database(value), error(exception) {}
    quint64 generation;
    std::shared_ptr<rdb::GeometryDatabase> database;
    std::exception_ptr error;
};

class DatabaseReadyEvent : public QEvent {
public:
    DatabaseReadyEvent(quint64 generationValue,
                       const std::shared_ptr<rdb::Database>& value,
                       const std::exception_ptr& exception)
        : QEvent(databaseReadyEventType), generation(generationValue), database(value), error(exception) {}
    quint64 generation;
    std::shared_ptr<rdb::Database> database;
    std::exception_ptr error;
};

class DetailRowsEvent : public QEvent {
public:
    DetailRowsEvent(quint64 generationValue,
                    quint64 requestValue,
                    const QVector<DetailRow>& rowValues)
        : QEvent(detailRowsEventType), generation(generationValue), request(requestValue), rows(rowValues) {}
    quint64 generation;
    quint64 request;
    QVector<DetailRow> rows;
};

struct DetailTaskResult {
    std::size_t parsed;
    bool completed;
    DetailTaskResult() : parsed(0), completed(false) {}
};

class DetailCompleteEvent : public QEvent {
public:
    DetailCompleteEvent(quint64 generationValue,
                        quint64 requestValue,
                        const DetailTaskResult& value,
                        const std::exception_ptr& exception)
        : QEvent(detailCompleteEventType),
          generation(generationValue),
          request(requestValue),
          result(value),
          error(exception) {}
    quint64 generation;
    quint64 request;
    DetailTaskResult result;
    std::exception_ptr error;
};

QString pointText(const rdb::Point& point) {
    return QStringLiteral("(%1, %2)").arg(QString::number(point.x), QString::number(point.y));
}

QString edgeText(const rdb::Edge& edge) {
    return QStringLiteral("(%1) — (%2)").arg(pointText(edge.first), pointText(edge.second));
}

void appendCoordinateText(QString& output, const QString& value) {
    const int maximumLength = 8192;
    if (output.size() >= maximumLength) return;
    if (!output.isEmpty()) output += QStringLiteral("  ");
    output += value;
    if (output.size() > maximumLength) {
        output.truncate(maximumLength - 1);
        output += QChar(0x2026);
    }
}

} // namespace

RdbViewer::RdbViewer(QWidget* parent)
    : QMainWindow(parent),
      mode_(RdbViewerMode::CoordinatesOnly),
      fileGeneration_(0),
      detailRequestId_(0),
      asyncState_(new RdbViewerAsyncState),
      checkModel_(new CheckTableModel(this)),
      detailModel_(new ResultTableModel(this)),
      treeModel_(new CheckTreeModel(this)),
      dock_(new QDockWidget(tr("RDB results"), this)),
      leftStack_(new QStackedWidget(dock_)),
      checkView_(new QTableView(leftStack_)),
      treeView_(new QTreeView(leftStack_)),
      detailView_(new QTableView(dock_)),
      treeSearch_(new QLineEdit(dock_)),
      statusLabel_(new QLabel(this)),
      indexProgress_(new QProgressBar(this)),
      rebuildingTree_(false) {
    setWindowTitle(tr("ASCII RDB Viewer"));
    resize(1200, 760);

    QLabel* welcome = new QLabel(
        tr("Open an RDB file from the File menu to inspect checks and results."), this);
    welcome->setAlignment(Qt::AlignCenter);
    setCentralWidget(welcome);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openCoordinates = fileMenu->addAction(tr("Open Coords Only…"));
    QAction* openParameters = fileMenu->addAction(tr("Open All Params…"));
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(tr("Quit"));
    connect(openCoordinates, &QAction::triggered, this,
            [this]() { openWithMode(RdbViewerMode::CoordinatesOnly); });
    connect(openParameters, &QAction::triggered, this,
            [this]() { openWithMode(RdbViewerMode::AllParameters); });
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    checkView_->setObjectName(QStringLiteral("checkTable"));
    treeView_->setObjectName(QStringLiteral("checkTree"));
    detailView_->setObjectName(QStringLiteral("detailTable"));
    checkView_->setModel(checkModel_);
    treeView_->setModel(treeModel_);
    detailView_->setModel(detailModel_);

    checkView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    checkView_->setSelectionMode(QAbstractItemView::SingleSelection);
    treeView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    treeView_->setSelectionMode(QAbstractItemView::SingleSelection);
    detailView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    detailView_->setTextElideMode(Qt::ElideRight);
    detailView_->setWordWrap(false);
    checkView_->setSortingEnabled(false);
    treeView_->setSortingEnabled(false);
    detailView_->setSortingEnabled(false);
    checkView_->horizontalHeader()->setStretchLastSection(true);
    treeView_->header()->setStretchLastSection(true);
    detailView_->horizontalHeader()->setStretchLastSection(true);
    treeView_->header()->setContextMenuPolicy(Qt::CustomContextMenu);

    leftStack_->addWidget(checkView_);
    QWidget* treePage = new QWidget(leftStack_);
    QVBoxLayout* treeLayout = new QVBoxLayout(treePage);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->addWidget(treeView_);
    QHBoxLayout* searchLayout = new QHBoxLayout;
    searchLayout->setContentsMargins(6, 4, 6, 4);
    treeSearch_->setObjectName(QStringLiteral("treeSearch"));
    treeSearch_->setPlaceholderText(tr("Find exact group name"));
    QPushButton* previousButton = new QPushButton(tr("Prev"), treePage);
    QPushButton* nextButton = new QPushButton(tr("Next"), treePage);
    searchLayout->addWidget(treeSearch_, 1);
    searchLayout->addWidget(previousButton);
    searchLayout->addWidget(nextButton);
    treeLayout->addLayout(searchLayout);
    leftStack_->addWidget(treePage);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, dock_);
    splitter->addWidget(leftStack_);
    splitter->addWidget(detailView_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    dock_->setWidget(splitter);
    dock_->setObjectName(QStringLiteral("rdbResultsDock"));
    addDockWidget(Qt::BottomDockWidgetArea, dock_);
    dock_->hide();

    indexProgress_->setRange(0, 100);
    indexProgress_->setValue(0);
    indexProgress_->setMaximumWidth(180);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(indexProgress_);

    // 배선도처럼 조밀한 데이터 화면이므로 선택 색만 청록으로 분명하게 구분한다.
    setStyleSheet(QStringLiteral(
        "QDockWidget::title { background: #203443; color: #f4f7f8; padding: 6px 9px; }"
        "QTableView::item:selected, QTreeView::item:selected { background: #0d7280; color: white; }"
        "QHeaderView::section { background: #e8eef0; color: #203443; padding: 5px; border: 0; }"));

    connect(checkView_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                showSelectedCheckRow(current.isValid() ? current.row() : -1);
            });
    connect(treeView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                showSelectedTreeNode(current);
            });
    connect(treeView_->header(), &QHeaderView::customContextMenuRequested,
            this, [this](const QPoint& point) { showGroupingMenu(point); });
    connect(previousButton, &QPushButton::clicked, this, [this]() { findTreeText(false); });
    connect(nextButton, &QPushButton::clicked, this, [this]() { findTreeText(true); });
    connect(treeSearch_, &QLineEdit::returnPressed, this, [this]() { findTreeText(true); });
}

RdbViewer::~RdbViewer() {
    // worker가 postEvent()를 끝낼 때까지 살아 있어 Qt 5.9에서도 lifetime race가 없다.
    cancelAllParsing();
    asyncState_->wait();
}

void RdbViewer::openFile(const QString& path) {
    openFile(path, RdbViewerMode::CoordinatesOnly);
}

void RdbViewer::openFile(const QString& path, RdbViewerMode mode) {
    if (path.isEmpty()) return;
    cancelAllParsing();
    ++fileGeneration_;
    ++detailRequestId_;
    mode_ = mode;
    selectedCheckRows_.clear();
    selectedConditions_.clear();
    fullDatabase_.reset();
    geometryDatabase_.reset();
    path_ = path;
    checkModel_->setIndex(rdb::CheckIndexDatabase());
    detailModel_->setMode(mode_);
    detailModel_->clear();
    treeModel_->clearTree();
    leftStack_->setCurrentIndex(mode_ == RdbViewerMode::CoordinatesOnly ? 0 : 1);
    indexProgress_->setValue(0);
    dock_->show();
    dock_->raise();
    statusLabel_->setText(mode_ == RdbViewerMode::CoordinatesOnly
        ? tr("Index and coordinate database are loading…")
        : tr("Index and full parameter database are loading…"));

    const quint64 generation = fileGeneration_;
    startIndexParsing(generation);
    startBackgroundParsing(generation);
}

bool RdbViewer::event(QEvent* event) {
    if (event->type() == indexProgressEventType) {
        IndexProgressEvent* progress = static_cast<IndexProgressEvent*>(event);
        if (progress->generation == fileGeneration_) indexProgress_->setValue(progress->progress);
        return true;
    }
    if (event->type() == indexReadyEventType) {
        IndexReadyEvent* ready = static_cast<IndexReadyEvent*>(event);
        if (ready->generation != fileGeneration_) return true;
        if (ready->error) {
            try {
                std::rethrow_exception(ready->error);
            } catch (const rdb::ScanCancelled&) {
            } catch (const std::exception& error) {
                reportError(tr("RDB index error"), error);
            }
            return true;
        }
        checkModel_->setIndex(*ready->index);
        indexProgress_->setValue(100);
        if (mode_ == RdbViewerMode::AllParameters && !fullDatabase_) rebuildIndexTree(true);
        statusLabel_->setText(tr("Check index ready: %1").arg(checkModel_->rowCount()));
        if (mode_ == RdbViewerMode::CoordinatesOnly && checkModel_->rowCount() > 0) {
            const QModelIndex first = checkModel_->index(0, 0);
            checkView_->selectionModel()->setCurrentIndex(
                first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else if (mode_ == RdbViewerMode::AllParameters && treeModel_->rowCount() > 0) {
            const QModelIndex first = treeModel_->index(0, 0);
            treeView_->selectionModel()->setCurrentIndex(
                first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        return true;
    }
    if (event->type() == geometryReadyEventType) {
        GeometryReadyEvent* ready = static_cast<GeometryReadyEvent*>(event);
        if (ready->generation != fileGeneration_) return true;
        if (ready->error) {
            try {
                std::rethrow_exception(ready->error);
            } catch (const rdb::GeometryParseCancelled&) {
            } catch (const std::exception& error) {
                reportError(tr("RDB coordinate parse error"), error);
            }
            return true;
        }
        geometryDatabase_ = ready->database;
        cancelSelectedDetail();
        ++detailRequestId_;
        statusLabel_->setText(tr("Coordinate background parsing complete"));
        showBackgroundDetail();
        return true;
    }
    if (event->type() == databaseReadyEventType) {
        DatabaseReadyEvent* ready = static_cast<DatabaseReadyEvent*>(event);
        if (ready->generation != fileGeneration_) return true;
        if (ready->error) {
            try {
                std::rethrow_exception(ready->error);
            } catch (const rdb::ParseCancelled&) {
            } catch (const std::exception& error) {
                reportError(tr("Full RDB parse error"), error);
            }
            return true;
        }
        fullDatabase_ = ready->database;
        cancelSelectedDetail();
        ++detailRequestId_;
        rebuildFullTree(true);
        statusLabel_->setText(tr("Full background parsing complete"));
        showBackgroundDetail();
        return true;
    }
    if (event->type() == detailRowsEventType) {
        DetailRowsEvent* rows = static_cast<DetailRowsEvent*>(event);
        if (rows->generation == fileGeneration_ && rows->request == detailRequestId_) {
            appendDetailRowsPreservingSelection(rows->rows);
            statusLabel_->setText(tr("Selected results loading: %1").arg(detailModel_->rowCount()));
        }
        return true;
    }
    if (event->type() == detailCompleteEventType) {
        DetailCompleteEvent* complete = static_cast<DetailCompleteEvent*>(event);
        if (complete->generation != fileGeneration_ || complete->request != detailRequestId_) return true;
        if (complete->error) {
            try {
                std::rethrow_exception(complete->error);
            } catch (const std::exception& error) {
                reportError(tr("Selected result parse error"), error);
            }
            return true;
        }
        if (complete->result.completed) {
            statusLabel_->setText(tr("Selected results complete: %1").arg(complete->result.parsed));
        }
        return true;
    }
    return QMainWindow::event(event);
}

void RdbViewer::openWithMode(RdbViewerMode mode) {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open ASCII RDB"), QString(), tr("ASCII RDB (*.rdb);;All files (*)"));
    if (!path.isEmpty()) openFile(path, mode);
}

void RdbViewer::startIndexParsing(quint64 fileGeneration) {
    const QString path = path_;
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    RdbViewer* const receiver = this;
    asyncState->start();
    std::thread([path, fileGeneration, receiver, cancellation, asyncState]() {
        AsyncTaskGuard guard(asyncState);
        std::shared_ptr<rdb::CheckIndexDatabase> index;
        std::exception_ptr error;
        try {
            rdb::FastCheckIndexOptions options;
            options.is_cancelled = [cancellation]() {
                return cancellation->load(std::memory_order_relaxed);
            };
            options.progress_callback = [fileGeneration, receiver, cancellation](int value) {
                if (!cancellation->load(std::memory_order_relaxed)) {
                    QCoreApplication::postEvent(receiver, new IndexProgressEvent(fileGeneration, value));
                }
            };
            rdb::FastCheckIndexParser parser;
            index.reset(new rdb::CheckIndexDatabase(parser.parse_database(path.toStdString(), options)));
        } catch (...) {
            error = std::current_exception();
        }
        QCoreApplication::postEvent(receiver, new IndexReadyEvent(fileGeneration, index, error));
    }).detach();
}

void RdbViewer::startBackgroundParsing(quint64 fileGeneration) {
    const QString path = path_;
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    RdbViewer* const receiver = this;
    if (mode_ == RdbViewerMode::CoordinatesOnly) {
        asyncState->start();
        std::thread([path, fileGeneration, receiver, cancellation, asyncState]() {
            AsyncTaskGuard guard(asyncState);
            std::shared_ptr<rdb::GeometryDatabase> database;
            std::exception_ptr error;
            try {
                rdb::GeometryParseOptions options;
                options.is_cancelled = [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                };
                rdb::CheckGeometryParser parser;
                database.reset(new rdb::GeometryDatabase(parser.parse_file(path.toStdString(), options)));
            } catch (...) {
                error = std::current_exception();
            }
            QCoreApplication::postEvent(receiver, new GeometryReadyEvent(fileGeneration, database, error));
        }).detach();
        return;
    }

    asyncState->start();
    std::thread([path, fileGeneration, receiver, cancellation, asyncState]() {
        AsyncTaskGuard guard(asyncState);
        std::shared_ptr<rdb::Database> database;
        std::exception_ptr error;
        try {
            rdb::ParseOptions options;
            options.is_cancelled = [cancellation]() {
                return cancellation->load(std::memory_order_relaxed);
            };
            rdb::AsciiRdbParser parser;
            database.reset(new rdb::Database(parser.parse_file(path.toStdString(), options)));
        } catch (...) {
            error = std::current_exception();
        }
        QCoreApplication::postEvent(receiver, new DatabaseReadyEvent(fileGeneration, database, error));
    }).detach();
}

void RdbViewer::showSelectedCheckRow(int row) {
    if (row < 0) return;
    QVector<int> rows;
    rows.append(row);
    showSelectedChecks(rows);
}

void RdbViewer::showSelectedTreeNode(const QModelIndex& current) {
    if (rebuildingTree_ || !current.isValid()) return;
    TreeSelection selection;
    if (!treeModel_->selectionForIndex(current, selection)) return;
    if (fullDatabase_) {
        selectedCheckRows_.clear();
        selectedConditions_ = selection.conditions;
        cancelSelectedDetail();
        ++detailRequestId_;
        detailModel_->clear();
        showBackgroundDetail();
    } else {
        showSelectedChecks(selection.checkRows);
    }
}

void RdbViewer::showSelectedChecks(const QVector<int>& rows) {
    selectedCheckRows_ = rows;
    selectedConditions_.clear();
    cancelSelectedDetail();
    const quint64 requestId = ++detailRequestId_;
    detailModel_->clear();
    if (rows.isEmpty()) return;
    if (geometryDatabase_ || fullDatabase_) {
        showBackgroundDetail();
    } else {
        startSelectedDetailParsing(rows, requestId);
    }
}

void RdbViewer::startSelectedDetailParsing(const QVector<int>& rows, quint64 requestId) {
    const QString path = path_;
    const quint64 fileGeneration = fileGeneration_;
    const RdbViewerMode mode = mode_;
    const rdb::CheckIndexDatabase index = checkModel_->indexDatabase();
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    RdbViewer* const receiver = this;
    selectedCancellation_ = cancellation;
    asyncState->start();

    std::thread([path, rows, index, mode, fileGeneration, requestId, cancellation, receiver, asyncState]() {
        AsyncTaskGuard guard(asyncState);
        DetailTaskResult outcome;
        std::exception_ptr error;
        try {
            for (int position = 0; position < rows.size(); ++position) {
                if (cancellation->load(std::memory_order_relaxed)) break;
                const int row = rows.at(position);
                if (row < 0 || static_cast<std::size_t>(row) >= index.checks.size()) continue;
                const rdb::CheckIndexEntry& entry = index.checks[static_cast<std::size_t>(row)];
                if (mode == RdbViewerMode::CoordinatesOnly) {
                    rdb::GeometryDetailBatchOptions options;
                    options.batch_size = 10000U;
                    options.is_cancelled = [cancellation]() {
                        return cancellation->load(std::memory_order_relaxed);
                    };
                    options.batch_callback = [fileGeneration, requestId, cancellation, receiver, entry](
                        const std::vector<rdb::GeometryDetailResult>& batch) {
                        if (cancellation->load(std::memory_order_relaxed)) return;
                        const QVector<DetailRow> rowsForEvent =
                            RdbViewer::rowsFromGeometryDetailBatch(batch, entry.offset);
                        QCoreApplication::postEvent(receiver,
                            new DetailRowsEvent(fileGeneration, requestId, rowsForEvent));
                    };
                    rdb::CheckGeometryDetailParser parser;
                    const rdb::GeometryDetailBatchResult parsed =
                        parser.parse_file_at_batches(path.toStdString(), entry.offset, options);
                    outcome.parsed += parsed.parsed_result_count;
                    if (!parsed.completed) break;
                } else {
                    rdb::CheckDetailBatchOptions options;
                    options.batch_size = 10000U;
                    options.is_cancelled = [cancellation]() {
                        return cancellation->load(std::memory_order_relaxed);
                    };
                    options.batch_callback = [fileGeneration, requestId, cancellation, receiver, entry](
                        const std::vector<rdb::DetailResult>& batch) {
                        if (cancellation->load(std::memory_order_relaxed)) return;
                        const QVector<DetailRow> rowsForEvent =
                            RdbViewer::rowsFromDetailBatch(batch, entry.offset);
                        QCoreApplication::postEvent(receiver,
                            new DetailRowsEvent(fileGeneration, requestId, rowsForEvent));
                    };
                    rdb::CheckDetailParser parser;
                    const rdb::CheckDetailBatchResult parsed =
                        parser.parse_file_at_batches(path.toStdString(), entry.offset, options);
                    outcome.parsed += parsed.parsed_result_count;
                    if (!parsed.completed) break;
                }
            }
            outcome.completed = !cancellation->load(std::memory_order_relaxed);
        } catch (...) {
            error = std::current_exception();
        }
        QCoreApplication::postEvent(
            receiver, new DetailCompleteEvent(fileGeneration, requestId, outcome, error));
    }).detach();
}

void RdbViewer::showBackgroundDetail() {
    if (mode_ == RdbViewerMode::CoordinatesOnly) {
        if (!geometryDatabase_ || selectedCheckRows_.isEmpty()) return;
        replaceDetailRowsPreservingSelection(
            rowsFromGeometryDatabase(*geometryDatabase_, selectedCheckRows_));
        return;
    }
    if (!fullDatabase_) return;
    replaceDetailRowsPreservingSelection(rowsFromDatabase());
}

void RdbViewer::replaceDetailRowsPreservingSelection(const QVector<DetailRow>& rows) {
    const TableSelectionSnapshot selection = TableSelectionKeeper::capture(*detailView_, *detailModel_);
    detailModel_->replaceRows(rows);
    TableSelectionKeeper::restore(*detailView_, *detailModel_, selection);
}

void RdbViewer::appendDetailRowsPreservingSelection(const QVector<DetailRow>& rows) {
    const TableSelectionSnapshot selection = TableSelectionKeeper::capture(*detailView_, *detailModel_);
    detailModel_->appendRows(rows);
    TableSelectionKeeper::restore(*detailView_, *detailModel_, selection);
}

void RdbViewer::cancelSelectedDetail() {
    if (selectedCancellation_) selectedCancellation_->store(true, std::memory_order_relaxed);
    selectedCancellation_.reset();
}

RdbViewer::CancellationToken RdbViewer::newCancellationToken() {
    const CancellationToken token(new std::atomic_bool(false));
    cancellationTokens_.push_back(token);
    return token;
}

void RdbViewer::cancelAllParsing() {
    for (std::vector<CancellationToken>::const_iterator it = cancellationTokens_.begin();
         it != cancellationTokens_.end(); ++it) {
        (*it)->store(true, std::memory_order_relaxed);
    }
    cancellationTokens_.clear();
    selectedCancellation_.reset();
}

void RdbViewer::reportError(const QString& title, const std::exception& error) {
    statusLabel_->setText(title);
    QMessageBox::critical(this, title, QString::fromLocal8Bit(error.what()));
}

void RdbViewer::rebuildIndexTree(bool preserveSelection) {
    if (mode_ != RdbViewerMode::AllParameters) return;
    const QString previous = preserveSelection ? currentTreeSelectionIdentity() : QString();
    rebuildingTree_ = true;
    treeModel_->rebuildIndex(checkModel_->indexDatabase());
    rebuildingTree_ = false;
    treeView_->expandAll();
    restoreTreeSelection(previous);
}

void RdbViewer::rebuildFullTree(bool preserveSelection) {
    if (!fullDatabase_ || mode_ != RdbViewerMode::AllParameters) return;
    const QString previous = preserveSelection ? currentTreeSelectionIdentity() : QString();
    rebuildingTree_ = true;
    treeModel_->rebuildFull(*fullDatabase_);
    rebuildingTree_ = false;
    treeView_->expandAll();
    restoreTreeSelection(previous);
}

QString RdbViewer::currentTreeSelectionIdentity() const {
    return treeModel_->selectionIdentity(treeView_->currentIndex());
}

void RdbViewer::restoreTreeSelection(const QString& identity) {
    QModelIndex target = treeModel_->indexForIdentity(identity);
    if (!target.isValid() && treeModel_->rowCount() > 0) target = treeModel_->index(0, 0);
    if (target.isValid()) {
        treeView_->selectionModel()->setCurrentIndex(
            target, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
}

void RdbViewer::showGroupingMenu(const QPoint& point) {
    if (!fullDatabase_ || mode_ != RdbViewerMode::AllParameters) return;
    QMenu menu(this);
    QAction* reset = menu.addAction(tr("Reset to Check Name"));
    connect(reset, &QAction::triggered, this, [this]() {
        const QString previous = currentTreeSelectionIdentity();
        treeModel_->resetGrouping();
        treeView_->expandAll();
        restoreTreeSelection(previous);
    });
    menu.addSeparator();

    std::vector<GroupingDimension> candidates;
    candidates.push_back(GroupingDimension(GroupingDimension::CheckName));
    const QStringList keys = treeModel_->availableTagKeys();
    for (const QString& key : keys) candidates.push_back(GroupingDimension(GroupingDimension::TaggedValueKey, key));
    for (int depth = 0; depth < 3; ++depth) {
        QMenu* depthMenu = menu.addMenu(tr("Grouping depth %1").arg(depth + 1));
        const std::vector<GroupingDimension>& grouping = treeModel_->grouping();
        const bool available = depth == 0 || static_cast<int>(grouping.size()) >= depth;
        for (std::vector<GroupingDimension>::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
            const GroupingDimension dimension = *it;
            const QString title = dimension.kind == GroupingDimension::CheckName
                ? tr("Check Name") : dimension.key;
            QAction* action = depthMenu->addAction(title);
            bool usedAtAnotherDepth = false;
            for (std::size_t selected = 0; selected < grouping.size(); ++selected) {
                if (static_cast<int>(selected) != depth && grouping[selected].equals(dimension)) {
                    usedAtAnotherDepth = true;
                }
            }
            action->setEnabled(available && !usedAtAnotherDepth);
            action->setCheckable(true);
            action->setChecked(depth < static_cast<int>(grouping.size()) &&
                               grouping[static_cast<std::size_t>(depth)].equals(dimension));
            connect(action, &QAction::triggered, this,
                    [this, depth, dimension]() {
                        const QString previous = currentTreeSelectionIdentity();
                        treeModel_->setGrouping(depth, dimension);
                        treeView_->expandAll();
                        restoreTreeSelection(previous);
                    });
        }
    }
    menu.exec(treeView_->header()->mapToGlobal(point));
}

void RdbViewer::findTreeText(bool forward) {
    const QString search = treeSearch_->text().trimmed();
    if (search.isEmpty() || treeModel_->rowCount() == 0) return;
    QList<QModelIndex> breadthFirst;
    QList<QModelIndex> queue;
    for (int row = 0; row < treeModel_->rowCount(); ++row) queue.append(treeModel_->index(row, 0));
    while (!queue.isEmpty()) {
        const QModelIndex item = queue.takeFirst();
        breadthFirst.append(item);
        for (int row = 0; row < treeModel_->rowCount(item); ++row) {
            queue.append(treeModel_->index(row, 0, item));
        }
    }
    if (breadthFirst.isEmpty()) return;
    int current = breadthFirst.indexOf(treeView_->currentIndex().sibling(
        treeView_->currentIndex().row(), 0));
    if (current < 0) current = forward ? -1 : 0;
    for (int step = 1; step <= breadthFirst.size(); ++step) {
        int candidate = forward ? (current + step) % breadthFirst.size()
                                : (current - step + breadthFirst.size() * 2) % breadthFirst.size();
        if (breadthFirst.at(candidate).data(Qt::DisplayRole).toString().compare(
                search, Qt::CaseInsensitive) == 0) {
            treeView_->selectionModel()->setCurrentIndex(
                breadthFirst.at(candidate), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            treeView_->scrollTo(breadthFirst.at(candidate));
            return;
        }
    }
}

bool RdbViewer::resultMatchesConditions(const rdb::Database& database,
                                        int checkRow,
                                        const rdb::Result& result,
                                        const std::vector<GroupCondition>& conditions) const {
    (void)database;
    return treeModel_->matchesConditions(checkRow, result, conditions);
}

QVector<DetailRow> RdbViewer::rowsFromDetailBatch(
    const std::vector<rdb::DetailResult>& batch, rdb::CheckOffset offset) {
    QVector<DetailRow> rows;
    if (batch.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("RDB detail batch exceeds Qt row capacity");
    }
    rows.reserve(static_cast<int>(batch.size()));
    for (std::vector<rdb::DetailResult>::const_iterator it = batch.begin(); it != batch.end(); ++it) {
        DetailRow row;
        row.key = resultKey(offset, it->kind, it->ordinal);
        row.resultLabel = QStringLiteral("%1 %2")
            .arg(it->kind == rdb::ResultKind::Polygon ? QStringLiteral("P") : QStringLiteral("E"))
            .arg(it->ordinal);
        for (std::vector<rdb::DetailTag>::const_iterator tag = it->properties_before_geometry.begin();
             tag != it->properties_before_geometry.end(); ++tag) {
            row.taggedValues[QString::fromStdString(tag->id)].append(
                QString::fromStdString(tag->payload));
        }
        for (std::vector<rdb::DetailTag>::const_iterator tag = it->properties_after_geometry.begin();
             tag != it->properties_after_geometry.end(); ++tag) {
            row.taggedValues[QString::fromStdString(tag->id)].append(
                QString::fromStdString(tag->payload));
        }
        rows.append(row);
    }
    return rows;
}

QVector<DetailRow> RdbViewer::rowsFromGeometryDetailBatch(
    const std::vector<rdb::GeometryDetailResult>& batch, rdb::CheckOffset offset) {
    QVector<DetailRow> rows;
    if (batch.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("RDB geometry detail batch exceeds Qt row capacity");
    }
    rows.reserve(static_cast<int>(batch.size()));
    for (std::vector<rdb::GeometryDetailResult>::const_iterator it = batch.begin(); it != batch.end(); ++it) {
        DetailRow row;
        row.key = resultKey(offset, it->kind, it->ordinal);
        row.coordinates = it->kind == rdb::ResultKind::Polygon
            ? coordinatesFromPoints(it->vertices) : coordinatesFromEdges(it->edges);
        rows.append(row);
    }
    return rows;
}

QVector<DetailRow> RdbViewer::rowsFromGeometryDatabase(
    const rdb::GeometryDatabase& database, const QVector<int>& rows) {
    QVector<DetailRow> output;
    for (int row = 0; row < rows.size(); ++row) {
        const int checkRow = rows.at(row);
        if (checkRow < 0 || static_cast<std::size_t>(checkRow) >= database.checks.size()) continue;
        const rdb::GeometryCheck& check = database.checks[static_cast<std::size_t>(checkRow)];
        for (rdb::Index index = 0; index < check.results.count; ++index) {
            const rdb::GeometryResult& result = database.results.at(check.results.begin + index);
            DetailRow detail;
            detail.key = resultKey(check.offset, result.kind, result.ordinal);
            QString text;
            if (result.kind == rdb::ResultKind::Polygon) {
                for (rdb::Index geometry = 0; geometry < result.geometry.count; ++geometry) {
                    appendCoordinateText(text, pointText(database.vertices.at(result.geometry.begin + geometry)));
                }
            } else {
                for (rdb::Index geometry = 0; geometry < result.geometry.count; ++geometry) {
                    appendCoordinateText(text, edgeText(database.edges.at(result.geometry.begin + geometry)));
                }
            }
            detail.coordinates = text;
            output.append(detail);
        }
    }
    return output;
}

QVector<DetailRow> RdbViewer::rowsFromDatabase() const {
    QVector<DetailRow> output;
    if (!fullDatabase_) return output;
    const rdb::Database& database = *fullDatabase_;
    for (std::size_t checkIndex = 0; checkIndex < database.rule_checks.size(); ++checkIndex) {
        if (selectedConditions_.empty() && !selectedCheckRows_.contains(static_cast<int>(checkIndex))) continue;
        const rdb::RuleCheck& check = database.rule_checks[checkIndex];
        const rdb::CheckOffset offset = static_cast<std::size_t>(checkIndex) < checkModel_->indexDatabase().checks.size()
            ? checkModel_->indexDatabase().checks[checkIndex].offset : 0;
        for (rdb::Index index = 0; index < check.results.count; ++index) {
            const rdb::Result& result = database.results.at(check.results.begin + index);
            if (!selectedConditions_.empty() && !resultMatchesConditions(
                    database, static_cast<int>(checkIndex), result, selectedConditions_)) continue;
            DetailRow row;
            row.key = resultKey(offset, result.kind, result.ordinal);
            row.resultLabel = QStringLiteral("%1 %2")
                .arg(result.kind == rdb::ResultKind::Polygon ? QStringLiteral("P") : QStringLiteral("E"))
                .arg(result.ordinal);
            appendTags(row, database, result.properties_before_geometry);
            appendTags(row, database, result.properties_after_geometry);
            output.append(row);
        }
    }
    return output;
}

QString RdbViewer::coordinatesFromPoints(const std::vector<rdb::Point>& points) {
    QString output;
    for (std::vector<rdb::Point>::const_iterator it = points.begin(); it != points.end(); ++it) {
        appendCoordinateText(output, pointText(*it));
    }
    return output;
}

QString RdbViewer::coordinatesFromEdges(const std::vector<rdb::Edge>& edges) {
    QString output;
    for (std::vector<rdb::Edge>::const_iterator it = edges.begin(); it != edges.end(); ++it) {
        appendCoordinateText(output, edgeText(*it));
    }
    return output;
}

QString RdbViewer::resultKey(rdb::CheckOffset offset, rdb::ResultKind kind, std::uint32_t ordinal) {
    return QStringLiteral("%1:%2:%3").arg(QString::number(offset))
        .arg(kind == rdb::ResultKind::Polygon ? QStringLiteral("p") : QStringLiteral("e"))
        .arg(ordinal);
}

void RdbViewer::appendTags(DetailRow& row, const rdb::Database& database, const rdb::Range& range) {
    for (rdb::Index offset = 0; offset < range.count; ++offset) {
        const rdb::TaggedValue& tag = database.tagged_values.at(range.begin + offset);
        row.taggedValues[QString::fromStdString(database.strings.get(tag.id).str())].append(
            QString::fromStdString(database.strings.get(tag.payload).str()));
    }
}
