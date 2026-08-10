#include "rdb_model.hpp"
#include "rdb_parser_workers.hpp"
#include "rdb_tree_model.hpp"

#include "rdb_check_index.hpp"

#include <QApplication>
#include <QByteArray>

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef RDB_SAMPLE_FILE
#define RDB_SAMPLE_FILE ""
#endif

namespace {

// 조건이 거짓이면 설명을 포함한 테스트 예외를 발생시킨다.
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// 샘플 파일의 Check Index를 Database 표현으로 변환한다.
RDB_DATABASE_PTR ParseIndex(const QString& path) {
    const QByteArray encoded = path.toLocal8Bit();
    return MakeDatabaseFromIndex(
        rdb::FastCheckIndexParser().parse_database(encoded.constData()));
}

// 전체 BG parser가 입력 Index DB와 분리된 DB만 수정하는지 검증한다.
void TestBackgroundDatabaseIsolation(
    const QString& path,
    const RDB_DATABASE_PTR& indexDatabase) {
    std::shared_ptr<std::atomic<bool> > interrupt(
        new std::atomic<bool>(false));
    RDBBackgroundParser parser(path, indexDatabase, interrupt);
    RDB_DATABASE_PTR completedDatabase;
    QString failure;
    bool cancelled = false;

    QObject::connect(
        &parser, &RDBBackgroundParser::Complete,
        [&completedDatabase](const RDB_DATABASE_PTR& database) {
            completedDatabase = database;
        });
    QObject::connect(
        &parser, &RDBBackgroundParser::ParsingFailed,
        [&failure](const QString& message) { failure = message; });
    QObject::connect(
        &parser, &RDBBackgroundParser::ParsingCancelled,
        [&cancelled]() { cancelled = true; });

    parser.run();

    Require(failure.isEmpty(), "background parser failed");
    Require(!cancelled, "background parser was unexpectedly cancelled");
    Require(completedDatabase.get() != 0, "background database is missing");
    Require(completedDatabase.get() != indexDatabase.get(),
            "background parser reused the GUI index database");
    Require(indexDatabase->loaded_check_count() == 0U,
            "background parser mutated the GUI index database");
    Require(completedDatabase->loaded_check_count() ==
                completedDatabase->check_count(),
            "background parser did not load every Check");
    Require(!completedDatabase->results.empty(),
            "background parser produced no results");
}

// 여러 Check 선택이 요청 순서대로 한 임시 DB에 적재되는지 검증한다.
void TestMultiCheckSelection(
    const QString& path,
    const RDB_DATABASE_PTR& indexDatabase) {
    Require(indexDatabase->check_count() >= 2U,
            "selection test requires at least two Checks");

    const quint64 requestId = 77U;
    std::vector<rdb::CheckId> checkIds;
    checkIds.push_back(static_cast<rdb::CheckId>(0U));
    checkIds.push_back(static_cast<rdb::CheckId>(1U));

    RDB_DATABASE_PTR selectionDatabase = CloneIndexDatabase(indexDatabase);
    RDBModel selectionModel(RDBModel::ALL_PARAMS);
    selectionModel.SetDatabase(selectionDatabase);
    selectionModel.SetActiveChecks(checkIds);

    std::shared_ptr<std::atomic<bool> > interrupt(
        new std::atomic<bool>(false));
    RDBDetailParser parser(
        path, indexDatabase, checkIds, requestId, interrupt);
    QString failure;
    bool completed = false;
    bool cancelled = false;
    std::size_t startedCount = 0U;
    std::size_t finishedCount = 0U;

    QObject::connect(
        &parser, &RDBDetailParser::CheckParsingStarted,
        [&selectionModel, &startedCount](
            quint64 receivedRequestId, quint64 checkIndex) {
            Require(receivedRequestId == requestId,
                    "selection start used a stale request ID");
            ++startedCount;
            selectionModel.BeginCheckLoad(
                static_cast<rdb::CheckId>(checkIndex));
        });
    QObject::connect(
        &parser, &RDBDetailParser::BatchReady,
        [&selectionModel](
            quint64 receivedRequestId,
            quint64 checkIndex,
            const RDB_DETAIL_BATCH_PTR& batch) {
            Require(receivedRequestId == requestId,
                    "selection batch used a stale request ID");
            selectionModel.AppendCoords(
                static_cast<rdb::CheckId>(checkIndex), batch);
        });
    QObject::connect(
        &parser, &RDBDetailParser::CheckParsingComplete,
        [&selectionModel, &finishedCount](
            quint64 receivedRequestId,
            quint64 checkIndex,
            const RDB_CHECK_DETAIL_PTR& detail) {
            Require(receivedRequestId == requestId,
                    "selection completion used a stale request ID");
            Require(detail.get() != 0, "selection detail is missing");
            ++finishedCount;
            selectionModel.FinishCheckLoad(
                static_cast<rdb::CheckId>(checkIndex), *detail);
        });
    QObject::connect(
        &parser, &RDBDetailParser::Complete,
        [&completed](quint64 receivedRequestId) {
            Require(receivedRequestId == requestId,
                    "selection completion used a stale request ID");
            completed = true;
        });
    QObject::connect(
        &parser, &RDBDetailParser::ParsingFailed,
        [&failure](quint64, const QString& message) { failure = message; });
    QObject::connect(
        &parser, &RDBDetailParser::ParsingCancelled,
        [&cancelled](quint64) { cancelled = true; });

    parser.run();

    Require(failure.isEmpty(), "multi-Check selection parser failed");
    Require(completed, "multi-Check selection did not complete");
    Require(!cancelled, "multi-Check selection was unexpectedly cancelled");
    Require(startedCount == checkIds.size(),
            "multi-Check selection did not start every Check");
    Require(finishedCount == checkIds.size(),
            "multi-Check selection did not finish every Check");
    Require(selectionDatabase->loaded_check_count() == checkIds.size(),
            "multi-Check selection did not load every selected Check");
    Require(selectionModel.TotalRowCount() != 0U,
            "multi-Check selection is not visible in the table model");
    Require(indexDatabase->loaded_check_count() == 0U,
            "selection parser mutated the GUI index database");
}

// 시작 전에 interrupt된 선택 parser가 취소만 알리는지 검증한다.
void TestSelectionCancellation(
    const QString& path,
    const RDB_DATABASE_PTR& indexDatabase) {
    std::vector<rdb::CheckId> checkIds(
        1U, static_cast<rdb::CheckId>(0U));
    std::shared_ptr<std::atomic<bool> > interrupt(
        new std::atomic<bool>(true));
    RDBDetailParser parser(path, indexDatabase, checkIds, 91U, interrupt);
    bool completed = false;
    bool cancelled = false;

    QObject::connect(
        &parser, &RDBDetailParser::Complete,
        [&completed](quint64) { completed = true; });
    QObject::connect(
        &parser, &RDBDetailParser::ParsingCancelled,
        [&cancelled](quint64 requestId) {
            Require(requestId == 91U,
                    "cancelled selection used a stale request ID");
            cancelled = true;
        });

    parser.run();

    Require(cancelled, "selection cancellation signal was not emitted");
    Require(!completed, "cancelled selection emitted completion");
}

// 이름이 같은 여러 Check가 한 Tree 행과 정확한 결과 목록을 유지하는지 검증한다.
void TestDuplicateCheckNameTreeRow() {
    rdb::CheckIndexDatabase index;
    index.top_cell_name = "TOP";
    index.database_precision = 0.001;

    rdb::CheckIndexEntry first;
    first.name = "DUPLICATE.CHECK";
    first.geometry_count = 2U;
    index.checks.push_back(first);

    rdb::CheckIndexEntry second;
    second.name = "DUPLICATE.CHECK";
    second.offset = 128U;
    second.geometry_count = 3U;
    index.checks.push_back(second);

    RDBTreeModel tree;
    tree.SetDatabase(MakeDatabaseFromIndex(index));
    Require(tree.rowCount() == 1,
            "duplicate CheckName was not grouped into one Tree row");
    const QModelIndex row = tree.index(0, RDBTreeModel::KEY);
    const std::vector<rdb::CheckId> checkIds = tree.GetCheckIds(row);
    Require(checkIds.size() == 2U,
            "grouped Tree row did not retain every Check target");
    Require(tree.data(tree.index(0, RDBTreeModel::VALUE)).toULongLong() == 5U,
            "grouped Tree row count is incorrect");

    const RDB_DATABASE_PTR loadedDatabase = tree.GetDatabase();
    loadedDatabase->results.resize(5U);
    loadedDatabase->rule_checks[0].results = rdb::Range(0U, 2U);
    loadedDatabase->rule_checks[0].detail_loaded = true;
    loadedDatabase->rule_checks[1].results = rdb::Range(2U, 3U);
    loadedDatabase->rule_checks[1].detail_loaded = true;
    loadedDatabase->loaded_rule_check_count = 2U;
    tree.SetDatabase(loadedDatabase);

    const QModelIndex loadedRow = tree.index(0, RDBTreeModel::KEY);
    const std::vector<rdb::Index> resultIndices =
        tree.GetResultIndices(loadedRow);
    Require(tree.rowCount() == 1 &&
                tree.HasExactResultSelection(loadedRow),
            "loaded duplicate CheckName fast path lost exact selection");
    Require(resultIndices.size() == 5U,
            "loaded duplicate CheckName fast path lost Results");
    for (std::size_t i = 0; i < resultIndices.size(); ++i) {
        Require(resultIndices[i] == static_cast<rdb::Index>(i),
                "loaded duplicate CheckName changed Result ordering");
    }
}

} // namespace

// Parser worker의 완료·취소·Database 격리 계약을 순차 검증한다.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    Q_UNUSED(application)

    try {
        const QString sample = argc > 1
            ? QString::fromLocal8Bit(argv[1])
            : QString::fromLocal8Bit(RDB_SAMPLE_FILE);
        Require(!sample.isEmpty(), "sample RDB path is empty");
        const RDB_DATABASE_PTR indexDatabase = ParseIndex(sample);
        Require(indexDatabase->loaded_check_count() == 0U,
                "new index database already contains detail data");

        TestBackgroundDatabaseIsolation(sample, indexDatabase);
        TestMultiCheckSelection(sample, indexDatabase);
        TestSelectionCancellation(sample, indexDatabase);
        TestDuplicateCheckNameTreeRow();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
