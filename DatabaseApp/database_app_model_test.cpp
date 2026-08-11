#include "rdb_database_support.hpp"
#include "rdb_model.hpp"
#include "rdb_tree_model.hpp"

#include "rdb_check_index.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>

#include <exception>
#include <iostream>

#ifndef RDB_SAMPLE_FILE
#define RDB_SAMPLE_FILE ""
#endif

namespace {

// 실패 메시지를 출력하고 테스트 실패 코드를 반환한다.
int Fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

// Result property Range에 지정 이름과 payload의 태그가 있는지 검사한다.
bool HasProperty(
    const RDB_DATABASE_PTR& database,
    const rdb::Result& result,
    const QString& name,
    const QString& payload) {
    for (std::size_t i = 0; i < result.properties.count; ++i) {
        const rdb::TaggedValue& value = database->tagged_values[
            static_cast<std::size_t>(result.properties.begin) + i];
        if (RDBString(database->strings, value.id) == name &&
            RDBString(database->strings, value.payload) == payload) {
            return true;
        }
    }
    return false;
}

} // namespace

// 공유 Database, TableModel, TreeModel의 핵심 계약을 회귀 검증한다.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        const QString sample = argc > 1
            ? QString::fromLocal8Bit(argv[1])
            : QString::fromLocal8Bit(RDB_SAMPLE_FILE);
        if (sample.isEmpty()) return Fail("sample RDB path is empty");
        const QByteArray encoded = QFile::encodeName(sample);
        const rdb::CheckIndexDatabase parsed =
            rdb::FastCheckIndexParser().parse_database(encoded.constData());
        const RDB_DATABASE_PTR database = MakeDatabaseFromIndex(parsed);
        if (!database || database->rule_checks.empty()) {
            return Fail("sample Check index is empty");
        }

        RDBModel chipModel(RDBModel::CHIP_TABLE);
        RDBModel detailModel(RDBModel::ALL_PARAMS);
        RDBModel backgroundModel(RDBModel::ALL_PARAMS);
        RDBTreeModel treeModel;
        chipModel.SetDatabase(database);
        detailModel.SetDatabase(database);
        backgroundModel.SetDatabase(database);
        treeModel.SetDatabase(database);

        if (chipModel.GetDatabase() != detailModel.GetDatabase() ||
            detailModel.GetDatabase() != backgroundModel.GetDatabase() ||
            treeModel.GetDatabase() != database) {
            return Fail("models do not share one Database");
        }
        if (chipModel.rowCount() !=
            static_cast<int>(database->rule_checks.size())) {
            return Fail("Check table row count mismatch");
        }
        if (chipModel.columnCount() != RDBModel::CHECK_TABLE_COL_COUNT) {
            return Fail("Check table column contract mismatch");
        }

        detailModel.SetActiveCheck(0U);
        rdb::CheckDetailFile detailFile(encoded.constData());
        for (std::size_t i = 0; i < database->rule_checks.size(); ++i) {
            const rdb::CheckId checkId = static_cast<rdb::CheckId>(i);
            detailModel.BeginCheckLoad(checkId);
            rdb::CheckDetailBatchOptions options;
            options.batch_size = 10000U;
            options.batch_callback =
                [&detailModel, checkId](
                    const std::vector<rdb::DetailResult>& values) {
                    RDB_DETAIL_BATCH_PTR batch(
                        new RDB_DETAIL_BATCH(
                            values,
                            std::shared_ptr<std::atomic<std::size_t> >()));
                    detailModel.AppendCoords(checkId, batch);
                };
            const rdb::CheckDetailBatchResult result =
                detailFile.parse_at_batches(
                    database->rule_checks[i].offset, options);
            if (!result.completed) return Fail("Detail parser was cancelled");
            detailModel.FinishCheckLoad(checkId, result.detail);
        }

        if (database->loaded_check_count() != database->check_count()) {
            return Fail("not all Checks were stored in Database");
        }
        if (database->results.empty()) {
            return Fail("sample Detail result list is empty");
        }
        const rdb::RuleCheck& firstCheck = database->rule_checks[0];
        if (firstCheck.results.count < 2U ||
            !HasProperty(
                database,
                database->results[
                    static_cast<std::size_t>(firstCheck.results.begin) + 1U],
                QStringLiteral("CN"),
                QStringLiteral("INV_X1"))) {
            return Fail("inherited CN was not stored in the Database");
        }
        detailModel.SetActiveCheck(0U);
        if (detailModel.TotalRowCount() !=
            database->rule_checks[0].results.count) {
            return Fail("Detail table does not reference Check Result range");
        }

        treeModel.BuildRDBTree();
        if (treeModel.rowCount() == 0) {
            return Fail("group Tree is empty after Detail load");
        }
        const QModelIndex first = treeModel.index(0, 0);
        const std::vector<rdb::Index> defaultResultIndices =
            treeModel.GetResultIndices(first);
        if (!first.isValid() ||
            !treeModel.HasExactResultSelection(first) ||
            defaultResultIndices.empty() ||
            defaultResultIndices.size() !=
                treeModel.data(
                    treeModel.index(first.row(), RDBTreeModel::VALUE))
                    .toULongLong()) {
            return Fail("Tree node does not expose Result indices");
        }

        const QStringList categories = treeModel.GetAvailableCategories();
        if (!categories.contains(QStringLiteral("PP")) ||
            !categories.contains(QStringLiteral("CN")) ||
            treeModel.GetAvailableCategories() != categories) {
            return Fail("Tree grouping category cache is incorrect");
        }

        treeModel.SetCompKeys(
            QStringList() << QStringLiteral("Check Name")
                          << QStringLiteral("Type"));
        for (int row = 0; row < treeModel.rowCount(); ++row) {
            const QModelIndex checkNode = treeModel.index(row, 0);
            const quint64 checkCount = treeModel.data(
                treeModel.index(row, RDBTreeModel::VALUE)).toULongLong();
            if (treeModel.GetResultIndices(checkNode).size() != checkCount) {
                return Fail("multi-depth Tree Check count is incorrect");
            }
            quint64 childCount = 0U;
            for (int child = 0;
                 child < treeModel.rowCount(checkNode); ++child) {
                const QModelIndex typeNode = treeModel.index(
                    child, 0, checkNode);
                const quint64 typeCount = treeModel.data(treeModel.index(
                    child, RDBTreeModel::VALUE, checkNode)).toULongLong();
                if (treeModel.GetResultIndices(typeNode).size() != typeCount) {
                    return Fail("multi-depth Tree Type count is incorrect");
                }
                childCount += typeCount;
            }
            if (childCount != checkCount) {
                return Fail("multi-depth Tree child count sum is incorrect");
            }
        }

        treeModel.SetCompKeys(QStringList() << QStringLiteral("PP"));
        quint64 propertyCount = 0U;
        bool foundPropertyValue = false;
        for (int row = 0; row < treeModel.rowCount(); ++row) {
            const QModelIndex propertyNode = treeModel.index(row, 0);
            const quint64 nodeCount = treeModel.data(treeModel.index(
                row, RDBTreeModel::VALUE)).toULongLong();
            if (treeModel.GetResultIndices(propertyNode).size() != nodeCount) {
                return Fail("Property Tree count is incorrect");
            }
            if (treeModel.data(propertyNode).toString().contains(
                    QStringLiteral("M1 spacing marker"))) {
                foundPropertyValue = true;
            }
            propertyCount += nodeCount;
        }
        if (!foundPropertyValue || propertyCount != database->results.size()) {
            return Fail("Property StringId grouping changed Tree values");
        }

        treeModel.SetCompKeys(
            QStringList() << QStringLiteral("Check Name"));
        if (treeModel.GetResultIndices(treeModel.index(0, 0)) !=
            defaultResultIndices) {
            return Fail("Check Name fast path changed Result ordering");
        }
        treeModel.InitSearch(QStringLiteral("M1"));
        const int firstSearchCount = treeModel.SearchCount();
        if (firstSearchCount == 0 ||
            !treeModel.GetSearchIndex().isValid() ||
            !treeModel.SearchNext(-1).isValid()) {
            return Fail("Tree search result list is incorrect");
        }
        treeModel.InitSearch(QStringLiteral("value-that-does-not-exist"));
        if (treeModel.SearchCount() != 0 ||
            treeModel.GetSearchIndex().isValid()) {
            return Fail("Tree search did not clear prior results");
        }
        treeModel.InitSearch(QStringLiteral("M1"));
        if (treeModel.SearchCount() != firstSearchCount) {
            return Fail("Repeated Tree search changed result count");
        }

        const std::size_t resultCount = database->results.size();
        const std::size_t vertexCount = database->vertices.size();
        detailModel.Clear();
        if (chipModel.GetDatabase()->results.size() != resultCount ||
            chipModel.GetDatabase()->vertices.size() != vertexCount) {
            return Fail("clearing one Model changed the shared Database owner");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
