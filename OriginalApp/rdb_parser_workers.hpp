#ifndef ORIGINAL_APP_RDB_PARSER_WORKERS_HPP
#define ORIGINAL_APP_RDB_PARSER_WORKERS_HPP

#include "rdb_types.hpp"

#include <QObject>

#include <atomic>
#include <memory>

class CalibreTextDock;

namespace rdb {
struct DetailResult;
}

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
    void CompleteBgParsing(const RDB_INDEX_RESULT_PTR& result);
    void ParsingFailed(const QString& message);
    void ParsingCancelled();

private:
    CalibreTextDock* doc_;
    QString path_;
    quint64 size_;
    std::shared_ptr<std::atomic<bool> > interrupt_;
};

class RDBDetailParser : public QObject {
    Q_OBJECT

public:
    RDBDetailParser(
        const QString& path,
        const RDB_DATA_LIST& chips,
        const std::shared_ptr<std::atomic<bool> >& interrupt,
        QObject* parent = 0);

public slots:
    void run();

signals:
    void BatchReady(
        quint64 checkIndex,
        const RDB_ALL_DATA_LIST& values,
        const QStringList& headers);
    void CheckParsingComplete(quint64 checkIndex);
    void Complete();
    void ParsingFailed(const QString& message);
    void ParsingCancelled();

private:
    RDB_ALL_DATA_PTR ConvertResult(
        const rdb::DetailResult& source,
        const RDB_DATA_PTR& check);
    void UpdateBoundingBox(RDB_ALL_DATA& value) const;

    QString path_;
    RDB_DATA_LIST chips_;
    quint64 next_result_index_;
    std::shared_ptr<std::atomic<bool> > interrupt_;
};

#endif // ORIGINAL_APP_RDB_PARSER_WORKERS_HPP
