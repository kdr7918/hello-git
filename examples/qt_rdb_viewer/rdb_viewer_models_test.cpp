#include "rdb_viewer_models.hpp"

#include <QItemSelectionModel>
#include <QTableView>
#include <QtTest>

#include <limits>
#include <stdexcept>

class RdbViewerModelsTest : public QObject {
    Q_OBJECT

private slots:
    void checkModelReceivesParsedIndex();
    void detailModelAppendsBatch();
    void detailModelRejectsAccumulatedRowsBeyondQtCapacity();
    void replacementRestoresMultiSelectionAndCurrentRow();
};

void RdbViewerModelsTest::checkModelReceivesParsedIndex() {
    rdb::CheckIndexDatabase index;
    index.top_cell_name = "TOP";
    index.database_precision = 0.001;
    rdb::CheckIndexEntry first;
    first.name = "M1.SPACING";
    first.offset = 123;
    first.geometry_count = 7;
    first.comment = "minimum spacing";
    index.checks.push_back(first);

    CheckTableModel model;
    model.setIndex(index);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0, CheckTableModel::Name).data().toString(), QString("M1.SPACING"));
    QCOMPARE(model.index(0, CheckTableModel::ResultCount).data().toUInt(), 7U);
    QCOMPARE(model.entryAt(0).offset, rdb::CheckOffset(123));
}

void RdbViewerModelsTest::detailModelAppendsBatch() {
    DetailTableModel model;
    QVector<DetailRow> batch;
    batch << DetailRow::polygon(1, 4, "first")
          << DetailRow::edgeCluster(2, 3, "second");

    model.appendRows(batch);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, DetailTableModel::Kind).data().toString(), QString("Polygon"));
    QCOMPARE(model.index(1, DetailTableModel::GeometryCount).data().toUInt(), 3U);
}

void RdbViewerModelsTest::detailModelRejectsAccumulatedRowsBeyondQtCapacity() {
    DetailTableModel::validateRowCapacity(std::numeric_limits<int>::max() - 5, 5);
    QVERIFY_EXCEPTION_THROWN(
        DetailTableModel::validateRowCapacity(std::numeric_limits<int>::max() - 5, 6),
        std::length_error);
    QVERIFY_EXCEPTION_THROWN(DetailTableModel::validateRowCapacity(0, -1), std::length_error);
}

void RdbViewerModelsTest::replacementRestoresMultiSelectionAndCurrentRow() {
    DetailTableModel model;
    QTableView view;
    view.setModel(&model);
    model.replaceRows(QVector<DetailRow>()
        << DetailRow::polygon(1, 4, "old-1")
        << DetailRow::edgeCluster(2, 2, "old-2")
        << DetailRow::polygon(3, 8, "old-3"));

    view.selectionModel()->select(
        model.index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->select(
        model.index(2, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(
        model.index(2, 0), QItemSelectionModel::NoUpdate);
    const TableSelectionSnapshot snapshot = TableSelectionKeeper::capture(view, model);

    model.replaceRows(QVector<DetailRow>()
        << DetailRow::polygon(9, 1, "new")
        << DetailRow::edgeCluster(2, 2, "background-2")
        << DetailRow::polygon(3, 8, "background-3"));
    TableSelectionKeeper::restore(view, model, snapshot);

    const QModelIndexList selected = view.selectionModel()->selectedRows();
    QCOMPARE(selected.size(), 2);
    QCOMPARE(model.rowAt(selected[0].row()).ordinal, quint32(2));
    QCOMPARE(model.rowAt(selected[1].row()).ordinal, quint32(3));
    QCOMPARE(model.rowAt(view.currentIndex().row()).ordinal, quint32(3));
}

QTEST_MAIN(RdbViewerModelsTest)
#include "rdb_viewer_models_test.moc"
