#ifndef DATABASE_APP_RDB_PARSER_WORKERS_HPP
#define DATABASE_APP_RDB_PARSER_WORKERS_HPP

#include "rdb_database.hpp"

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
    /// Check Index 파일 경로와 공유 interrupt flag를 보관한다.
    BgParser(
        CalibreTextDock* doc,
        const QString& path,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    /// Worker thread에서 전체 파일의 Check Index와 진행률을 생성한다.
    void run();

signals:
    /// Check Index 스캔 진행률(0~100)을 GUI thread에 전달한다.
    void ProgressChanged(int value);
    /// 완성된 Index Database를 GUI thread에 전달한다.
    void CompleteBgParsing(const RDB_DATABASE_PTR& database);
    /// 파서 예외 메시지를 GUI thread에 전달한다.
    void ParsingFailed(const QString& message);
    /// interrupt flag에 따른 정상 취소를 알린다.
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
    /// Index DB를 복제해 전체 Detail 파싱 전용 worker 상태를 만든다.
    RDBBackgroundParser(
        const QString& path,
        const RDB_DATABASE_PTR& indexDatabase,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    /// Worker thread에서 모든 Check Detail을 처음부터 끝까지 적재한다.
    void run();

signals:
    /// 모든 Check가 적재된 전용 Database를 한 번 전달한다.
    void Complete(const RDB_DATABASE_PTR& database);
    /// 전체 Detail 파서의 오류 메시지를 전달한다.
    void ParsingFailed(const QString& message);
    /// 전체 Detail 파서가 interrupt로 종료됐음을 알린다.
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
    /// 선택된 CheckId와 offset만 추려 요청 단위 worker 상태를 만든다.
    RDBDetailParser(
        const QString& path,
        const RDB_DATABASE_PTR& indexDatabase,
        const std::vector<rdb::CheckId>& checkIds,
        quint64 requestId,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    /// Worker thread에서 선택 Check들을 순서대로 배치 파싱한다.
    void run();

signals:
    /// 한 Check의 배치 적재가 시작됨을 요청 번호와 함께 알린다.
    void CheckParsingStarted(quint64 requestId, quint64 checkIndex);
    /// 최대 10,000개 Detail 결과 배치를 GUI thread에 전달한다.
    void BatchReady(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_DETAIL_BATCH_PTR& batch);
    /// 한 Check의 모든 배치와 메타데이터 파싱 완료를 알린다.
    void CheckParsingComplete(
        quint64 requestId,
        quint64 checkIndex,
        const RDB_CHECK_DETAIL_PTR& detail);
    /// 요청에 포함된 모든 Check가 완료됐음을 알린다.
    void Complete(quint64 requestId);
    /// 선택 파서 오류를 요청 번호와 함께 전달한다.
    void ParsingFailed(quint64 requestId, const QString& message);
    /// 선택 파서가 interrupt로 종료됐음을 요청 번호와 함께 알린다.
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
