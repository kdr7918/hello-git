#ifndef DATABASE_APP_RDB_PARSER_WORKERS_HPP
#define DATABASE_APP_RDB_PARSER_WORKERS_HPP

#include "rdb_database_support.hpp"

#include <QObject>

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

class CalibreTextDock;

// 최초 Check Index 전용 worker다.
class BgParser : public QObject {
    Q_OBJECT

public:
    BgParser(
        CalibreTextDock* doc,
        const QString& path,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    void run();

signals:
    void ProgressChanged(int value);
    void CompleteBgParsing(const RDB_DATABASE_PTR& database);
    void ParsingFailed(const QString& message);
    void ParsingCancelled();

private:
    CalibreTextDock* doc_;
    QString path_;
    quint64 size_;
    std::shared_ptr<std::atomic<bool> > interrupt_;
};

// 모든 Check를 처음부터 끝까지 전용 Database에 적재한다. GUI Database는
// 완료될 때 한 번만 교체되므로 선택 parser와 동시에 실행해도 data race가 없다.
class RDBBackgroundParser : public QObject {
    Q_OBJECT

public:
    RDBBackgroundParser(
        const QString& path,
        const RDB_DATABASE_PTR& indexDatabase,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    void run();

signals:
    void Complete(const RDB_DATABASE_PTR& database);
    void ParsingFailed(const QString& message);
    void ParsingCancelled();

private:
    QString path_;
    RDB_DATABASE_PTR database_;
    std::shared_ptr<std::atomic<bool> > interrupt_;
};

// BG 완료 전에 사용자가 선택한 Check만 임시 Database용으로 파싱한다.
class RDBDetailParser : public QObject {
    Q_OBJECT

public:
    RDBDetailParser(
        const QString& path,
        const RDB_DATABASE_PTR& indexDatabase,
        const std::vector<rdb::CheckId>& checkIds,
        quint64 requestId,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    void run();

signals:
    void CheckParsingStarted(quint64 requestId, quint64 checkIndex);
    void BatchReady(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    void CheckParsingComplete(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_CHECK_DETAIL_PTR& detail);
    void Complete(quint64 requestId);
    void ParsingFailed(quint64 requestId, const QString& message);
    void ParsingCancelled(quint64 requestId);

private:
    typedef std::pair<rdb::CheckId, rdb::CheckOffset> WorkItem;

    QString path_;
    std::vector<WorkItem> work_;
    quint64 request_id_;
    std::shared_ptr<std::atomic<bool> > interrupt_;
    std::shared_ptr<std::atomic<std::size_t> > pending_batches_;
};

#endif // DATABASE_APP_RDB_PARSER_WORKERS_HPP
