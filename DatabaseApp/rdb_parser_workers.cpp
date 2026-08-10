#include "rdb_parser_workers.hpp"

#include "rdb_model.hpp"
#include "rdb_check_index.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <exception>
#include <stdexcept>
#include <thread>

namespace {

const std::size_t maximum_pending_detail_batches = 2U;

} // namespace

// 최초 Check Index 전용 worker에 파일·interrupt 상태를 보관한다.
BgParser::BgParser(
    CalibreTextDock* doc,
    const QString& path,
    const std::shared_ptr<std::atomic<bool> >& interrupt,
    QObject* parent)
    : QObject(parent),
      doc_(doc),
      path_(path),
      size_(static_cast<quint64>(QFileInfo(path).size())),
      interrupt_(interrupt) {}

// Index 파싱을 worker thread에서 수행하고 결과·진행률만 signal로 전달한다.
void BgParser::run() {
    Q_UNUSED(doc_)
    Q_UNUSED(size_)
    try {
        const QByteArray encodedPath = QFile::encodeName(path_);
        rdb::FastCheckIndexOptions options;
        // callback은 worker thread에서 호출되며 Qt queued signal이 GUI 경계를 넘는다.
        options.progress_callback = [this](int progress) {
            emit ProgressChanged(progress);
        };
        options.is_cancelled = [this]() {
            return interrupt_ && interrupt_->load();
        };

        const rdb::CheckIndexDatabase parsed =
            rdb::FastCheckIndexParser().parse_database(
                encodedPath.constData(), options);
        if (interrupt_ && interrupt_->load()) {
            emit ParsingCancelled();
            return;
        }
        emit CompleteBgParsing(MakeDatabaseFromIndex(parsed));
    } catch (const rdb::ScanCancelled&) {
        emit ParsingCancelled();
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown Check index parser error"));
    }
}

// 전체 BG Detail 파서가 단독 소유할 Index-only Database 복제본을 만든다.
RDBBackgroundParser::RDBBackgroundParser(
    const QString& path,
    const RDB_DATABASE_PTR& indexDatabase,
    const std::shared_ptr<std::atomic<bool> >& interrupt,
    QObject* parent)
    : QObject(parent),
      path_(path),
      database_(CloneIndexDatabase(indexDatabase)),
      interrupt_(interrupt) {}

// 모든 Check Detail을 순차 파싱해 완성 DB 하나를 만든 뒤 한 번에 전달한다.
void RDBBackgroundParser::run() {
    try {
        const QByteArray encodedPath = QFile::encodeName(path_);
        rdb::CheckDetailFile detailFile(encodedPath.constData());
        // 이 모델은 worker thread 안에서만 사용하므로 GUI 모델과 동시 쓰기가 없다.
        RDBModel databaseWriter(RDBModel::ALL_PARAMS);
        databaseWriter.SetDatabase(database_);

        for (std::size_t checkNumber = 0;
             checkNumber < database_->rule_checks.size(); ++checkNumber) {
            if (interrupt_ && interrupt_->load()) {
                emit ParsingCancelled();
                return;
            }
            const rdb::CheckId checkId =
                static_cast<rdb::CheckId>(checkNumber);
            // Check 단위 checkpoint를 열어 취소·예외 시 부분 적재를 rollback한다.
            databaseWriter.BeginCheckLoad(checkId);

            rdb::CheckDetailBatchOptions options;
            options.batch_size = 10000U;
            options.is_cancelled = [this]() {
                return interrupt_ && interrupt_->load();
            };
            options.batch_callback =
                [&databaseWriter, checkId](
                    const std::vector<rdb::DetailResult>& values) {
                    RDB_DETAIL_BATCH_PTR batch(
                        new RDB_DETAIL_BATCH(
                            values,
                            std::shared_ptr<std::atomic<std::size_t> >()));
                    databaseWriter.AppendCoords(checkId, batch);
                };

            const rdb::CheckDetailBatchResult result =
                detailFile.parse_at_batches(
                    database_->rule_checks[checkNumber].offset, options);
            if (!result.completed || (interrupt_ && interrupt_->load())) {
                databaseWriter.CancelCheckLoad();
                emit ParsingCancelled();
                return;
            }
            databaseWriter.FinishCheckLoad(checkId, result.detail);
        }
        emit Complete(database_);
    } catch (const std::exception& error) {
        emit ParsingFailed(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(QStringLiteral("Unknown background RDB parser error"));
    }
}

// 선택 시점의 CheckId·offset을 복사해 원본 모델 변경과 worker 수명을 분리한다.
RDBDetailParser::RDBDetailParser(
    const QString& path,
    const RDB_DATABASE_PTR& indexDatabase,
    const std::vector<rdb::CheckId>& checkIds,
    quint64 requestId,
    const std::shared_ptr<std::atomic<bool> >& interrupt,
    QObject* parent)
    : QObject(parent),
      path_(path),
      request_id_(requestId),
      interrupt_(interrupt),
      pending_batches_(new std::atomic<std::size_t>(0U)) {
    if (!indexDatabase) throw std::invalid_argument("RDB Database is null");
    work_.reserve(checkIds.size());
    for (std::size_t i = 0; i < checkIds.size(); ++i) {
        const std::size_t checkIndex =
            static_cast<std::size_t>(checkIds[i]);
        if (checkIndex >= indexDatabase->rule_checks.size()) {
            throw std::out_of_range("Selected RDB CheckId is out of range");
        }
        work_.push_back(WorkItem(
            checkIds[i], indexDatabase->rule_checks[checkIndex].offset));
    }
}

// 선택된 Check만 파싱하며 배치를 GUI로 보내고 최신 requestId로 결과를 식별한다.
void RDBDetailParser::run() {
    try {
        const QByteArray encodedPath = QFile::encodeName(path_);
        rdb::CheckDetailFile detailFile(encodedPath.constData());
        for (std::size_t checkNumber = 0;
             checkNumber < work_.size(); ++checkNumber) {
            if (interrupt_ && interrupt_->load()) {
                emit ParsingCancelled(request_id_);
                return;
            }
            const rdb::CheckId checkId = work_[checkNumber].first;
            emit CheckParsingStarted(
                request_id_, static_cast<quint64>(checkId));

            rdb::CheckDetailBatchOptions options;
            options.batch_size = 10000U;
            options.is_cancelled = [this]() {
                return interrupt_ && interrupt_->load();
            };
            options.batch_callback =
                [this, checkId](
                    const std::vector<rdb::DetailResult>& values) {
                    // GUI가 느릴 때 미처리 배치를 두 개로 제한해 메모리 급증을 막는다.
                    while (pending_batches_->load() >=
                               maximum_pending_detail_batches &&
                           !(interrupt_ && interrupt_->load())) {
                        std::this_thread::yield();
                    }
                    if (interrupt_ && interrupt_->load()) return;
                    RDB_DETAIL_BATCH_PTR batch(
                        new RDB_DETAIL_BATCH(values, pending_batches_));
                    emit BatchReady(
                        request_id_, static_cast<quint64>(checkId), batch);
                };

            rdb::CheckDetailBatchResult result =
                detailFile.parse_at_batches(work_[checkNumber].second, options);
            if (!result.completed || (interrupt_ && interrupt_->load())) {
                emit ParsingCancelled(request_id_);
                return;
            }
            RDB_CHECK_DETAIL_PTR detail(
                new rdb::CheckDetail(std::move(result.detail)));
            emit CheckParsingComplete(
                request_id_, static_cast<quint64>(checkId), detail);
        }
        emit Complete(request_id_);
    } catch (const std::exception& error) {
        emit ParsingFailed(
            request_id_, QString::fromLocal8Bit(error.what()));
    } catch (...) {
        emit ParsingFailed(
            request_id_, QStringLiteral("Unknown selected RDB parser error"));
    }
}
