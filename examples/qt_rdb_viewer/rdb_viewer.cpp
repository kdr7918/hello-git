#include "rdb_viewer.hpp"

#include <QFutureWatcher>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QtConcurrent>

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>

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
} // namespace

RdbViewer::RdbViewer(QWidget* parent)
    : QMainWindow(parent),
      fileGeneration_(0),
      detailRequestId_(0),
      selectedCheckRow_(-1),
      asyncState_(new RdbViewerAsyncState),
      checkModel_(new CheckTableModel(this)),
      detailModel_(new DetailTableModel(this)),
      checkView_(new QTableView(this)),
      detailView_(new QTableView(this)),
      statusLabel_(new QLabel(this)),
      indexProgress_(new QProgressBar(this)) {
    setWindowTitle("ASCII RDB Viewer");
    checkView_->setObjectName(QStringLiteral("checkTable"));
    detailView_->setObjectName(QStringLiteral("detailTable"));
    checkView_->setModel(checkModel_);
    detailView_->setModel(detailModel_);
    checkView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    checkView_->setSelectionMode(QAbstractItemView::SingleSelection);
    detailView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    checkView_->setSortingEnabled(false);
    detailView_->setSortingEnabled(false);
    checkView_->horizontalHeader()->setStretchLastSection(true);
    detailView_->horizontalHeader()->setStretchLastSection(true);

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(checkView_);
    splitter->addWidget(detailView_);
    setCentralWidget(splitter);

    indexProgress_->setRange(0, 100);
    indexProgress_->setValue(0);
    indexProgress_->setMaximumWidth(180);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(indexProgress_);

    connect(checkView_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                showSelectedCheck(current.isValid() ? current.row() : -1);
            });
}

RdbViewer::~RdbViewer() {
    // QPointer null 확인과 invokeMethod 사이의 lifetime race를 없애기 위해
    // 모든 worker가 callback enqueue를 끝낼 때까지 객체를 살아 있게 둔다.
    cancelAllParsing();
    asyncState_->wait();
}

void RdbViewer::openFile(const QString& path) {
    cancelAllParsing();
    ++fileGeneration_;
    ++detailRequestId_;
    selectedCheckRow_ = -1;
    fullDatabase_.reset();
    path_ = path;
    checkModel_->setIndex(rdb::CheckIndexDatabase());
    detailModel_->clear();
    indexProgress_->setValue(0);
    statusLabel_->setText(tr("Index와 전체 상세를 동시에 파싱 중…"));

    const quint64 generation = fileGeneration_;
    startIndexParsing(generation);
    startFullBackgroundParsing(generation);
}

void RdbViewer::startIndexParsing(quint64 fileGeneration) {
    const QString path = path_;
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    asyncState->start();
    QPointer<RdbViewer> weakThis(this);
    QFutureWatcher<std::shared_ptr<rdb::CheckIndexDatabase> >* watcher =
        new QFutureWatcher<std::shared_ptr<rdb::CheckIndexDatabase> >(this);

    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, fileGeneration]() {
        try {
            const std::shared_ptr<rdb::CheckIndexDatabase> index = watcher->future().result();
            if (fileGeneration == fileGeneration_) {
                checkModel_->setIndex(*index);
                indexProgress_->setValue(100);
                statusLabel_->setText(tr("Check index 완료: %1개").arg(checkModel_->rowCount()));
                if (checkModel_->rowCount() > 0) {
                    const QModelIndex first = checkModel_->index(0, 0);
                    checkView_->selectionModel()->setCurrentIndex(
                        first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    checkView_->selectRow(0); // 요구사항: 첫 Check 자동 선택
                }
            }
        } catch (const std::exception& error) {
            if (fileGeneration == fileGeneration_) reportError(tr("RDB index 오류"), error);
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(
        [path, fileGeneration, weakThis, cancellation, asyncState]() {
        AsyncTaskGuard task(asyncState);
        rdb::FastCheckIndexOptions options;
        options.is_cancelled = [cancellation]() {
            return cancellation->load(std::memory_order_relaxed);
        };
        options.progress_callback = [fileGeneration, weakThis, cancellation](int value) {
            if (cancellation->load(std::memory_order_relaxed) || !weakThis) return;
            QMetaObject::invokeMethod(weakThis, [fileGeneration, weakThis, value]() {
                if (weakThis && weakThis->fileGeneration_ == fileGeneration) {
                    weakThis->indexProgress_->setValue(value);
                }
            }, Qt::QueuedConnection);
        };
        rdb::FastCheckIndexParser parser;
        return std::make_shared<rdb::CheckIndexDatabase>(
            parser.parse_database(path.toStdString(), options));
    }));
}

void RdbViewer::startFullBackgroundParsing(quint64 fileGeneration) {
    const QString path = path_;
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    asyncState->start();
    QFutureWatcher<std::shared_ptr<rdb::Database> >* watcher =
        new QFutureWatcher<std::shared_ptr<rdb::Database> >(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, fileGeneration]() {
        try {
            const std::shared_ptr<rdb::Database> database = watcher->future().result();
            if (fileGeneration == fileGeneration_) {
                fullDatabase_ = database;
                cancelSelectedDetail();
                ++detailRequestId_; // queued 상태의 이전 선택 batch도 무효화한다.
                statusLabel_->setText(tr("전체 Check 상세 백그라운드 파싱 완료"));
                if (selectedCheckRow_ >= 0) showBackgroundDetail(selectedCheckRow_);
            }
        } catch (const std::exception& error) {
            if (fileGeneration == fileGeneration_) reportError(tr("전체 RDB 파싱 오류"), error);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([path, cancellation, asyncState]() {
        AsyncTaskGuard task(asyncState);
        rdb::ParseOptions options;
        options.is_cancelled = [cancellation]() {
            return cancellation->load(std::memory_order_relaxed);
        };
        rdb::AsciiRdbParser parser;
        return std::make_shared<rdb::Database>(parser.parse_file(path.toStdString(), options));
    }));
}

void RdbViewer::showSelectedCheck(int row) {
    selectedCheckRow_ = row;
    cancelSelectedDetail();
    const quint64 requestId = ++detailRequestId_;
    detailModel_->clear();
    if (row < 0 || row >= checkModel_->rowCount()) return;

    if (fullDatabase_) {
        showBackgroundDetail(row);
    } else {
        startSelectedDetailParsing(row, requestId);
    }
}

void RdbViewer::startSelectedDetailParsing(int row, quint64 requestId) {
    const rdb::CheckIndexEntry entry = checkModel_->entryAt(row);
    const QString path = path_;
    const quint64 fileGeneration = fileGeneration_;
    const CancellationToken cancellation = newCancellationToken();
    const std::shared_ptr<RdbViewerAsyncState> asyncState = asyncState_;
    asyncState->start();
    selectedCancellation_ = cancellation;
    QPointer<RdbViewer> weakThis(this);

    QFutureWatcher<rdb::CheckDetailBatchResult>* watcher =
        new QFutureWatcher<rdb::CheckDetailBatchResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, fileGeneration, requestId]() {
        try {
            const rdb::CheckDetailBatchResult outcome = watcher->future().result();
            if (fileGeneration == fileGeneration_ && requestId == detailRequestId_ &&
                outcome.completed) {
                statusLabel_->setText(
                    tr("선택 Check 상세 완료: %1개").arg(outcome.parsed_result_count));
            }
        } catch (const std::exception& error) {
            if (fileGeneration == fileGeneration_ && requestId == detailRequestId_) {
                reportError(tr("선택 Check 상세 오류"), error);
            }
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(
        [path, entry, row, fileGeneration, requestId, cancellation, weakThis, asyncState]() {
            AsyncTaskGuard task(asyncState);
            rdb::CheckDetailBatchOptions options;
            options.batch_size = 10000U;
            options.is_cancelled = [cancellation]() {
                return cancellation->load(std::memory_order_relaxed);
            };
            options.batch_callback =
                [row, fileGeneration, requestId, cancellation, weakThis](
                    const std::vector<rdb::DetailResult>& batch) {
                    if (cancellation->load(std::memory_order_relaxed) || !weakThis) return;
                    const QVector<DetailRow> rows = RdbViewer::rowsFromDetailBatch(batch);
                    QMetaObject::invokeMethod(weakThis,
                        [row, fileGeneration, requestId, cancellation, weakThis, rows]() {
                            if (!weakThis || cancellation->load(std::memory_order_relaxed) ||
                                weakThis->fileGeneration_ != fileGeneration ||
                                weakThis->detailRequestId_ != requestId ||
                                weakThis->selectedCheckRow_ != row || weakThis->fullDatabase_) return;
                            weakThis->appendDetailRowsPreservingSelection(rows);
                            weakThis->statusLabel_->setText(
                                weakThis->tr("선택 Check 상세 로딩: %1개")
                                    .arg(weakThis->detailModel_->rowCount()));
                        }, Qt::QueuedConnection);
                };
            rdb::CheckDetailParser parser;
            return parser.parse_file_at_batches(path.toStdString(), entry.offset, options);
        }));
}

void RdbViewer::showBackgroundDetail(int row) {
    if (!fullDatabase_ || row < 0) return;
    std::size_t checkIndex = static_cast<std::size_t>(row);
    if (checkIndex >= fullDatabase_->rule_checks.size()) return;

    // 두 파서의 순서는 동일하지만 이름까지 확인해 잘못된 행 결합을 방지한다.
    const std::string expectedName = checkModel_->entryAt(row).name;
    if (fullDatabase_->strings.get(fullDatabase_->rule_checks[checkIndex].name).str() != expectedName) {
        checkIndex = fullDatabase_->rule_checks.size();
        for (std::size_t i = 0; i < fullDatabase_->rule_checks.size(); ++i) {
            if (fullDatabase_->strings.get(fullDatabase_->rule_checks[i].name).str() == expectedName) {
                checkIndex = i;
                break;
            }
        }
        if (checkIndex == fullDatabase_->rule_checks.size()) return;
    }
    replaceDetailRowsPreservingSelection(rowsFromDatabase(*fullDatabase_, checkIndex));
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

QString RdbViewer::resultSummary(const std::string& suffix,
                                 std::size_t beforeTagCount,
                                 std::size_t afterTagCount) {
    QString text = QString::fromStdString(suffix);
    if (!text.isEmpty()) text += QStringLiteral(" | ");
    text += tr("tags %1 + %2").arg(beforeTagCount).arg(afterTagCount);
    return text;
}

QVector<DetailRow> RdbViewer::rowsFromDetailBatch(
    const std::vector<rdb::DetailResult>& batch) {
    QVector<DetailRow> rows;
    if (batch.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("RDB detail batch exceeds Qt row capacity");
    }
    rows.reserve(static_cast<int>(batch.size()));
    for (std::vector<rdb::DetailResult>::const_iterator it = batch.begin(); it != batch.end(); ++it) {
        const quint32 count = static_cast<quint32>(
            it->kind == rdb::ResultKind::Polygon ? it->vertices.size() : it->edges.size());
        const QString summary = resultSummary(
            it->signature_suffix,
            it->properties_before_geometry.size(),
            it->properties_after_geometry.size());
        rows.push_back(it->kind == rdb::ResultKind::Polygon
            ? DetailRow::polygon(it->ordinal, count, summary)
            : DetailRow::edgeCluster(it->ordinal, count, summary));
    }
    return rows;
}

QVector<DetailRow> RdbViewer::rowsFromDatabase(
    const rdb::Database& database, std::size_t checkIndex) {
    QVector<DetailRow> rows;
    const rdb::RuleCheck& check = database.rule_checks.at(checkIndex);
    if (check.results.count > static_cast<rdb::Index>(std::numeric_limits<int>::max())) {
        throw std::length_error("RDB result count exceeds Qt model row capacity");
    }
    rows.reserve(static_cast<int>(check.results.count));
    for (rdb::Index i = 0; i < check.results.count; ++i) {
        const rdb::Result& result = database.results.at(check.results.begin + i);
        const QString summary = resultSummary(
            database.strings.get(result.signature_suffix).str(),
            result.properties_before_geometry.count,
            result.properties_after_geometry.count);
        rows.push_back(result.kind == rdb::ResultKind::Polygon
            ? DetailRow::polygon(result.ordinal, result.geometry.count, summary)
            : DetailRow::edgeCluster(result.ordinal, result.geometry.count, summary));
    }
    return rows;
}
