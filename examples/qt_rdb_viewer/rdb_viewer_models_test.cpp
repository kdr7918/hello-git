#include "rdb_viewer_models.hpp"

#include <QItemSelectionModel>
#include <QTableView>
#include <QtTest>

class RdbViewerModelsTest : public QObject {
    Q_OBJECT

private slots:
    void checkModelUsesRequestedColumns();
    void coordinateResultUsesSingleColumn();
    void parameterResultCreatesKeyColumns();
    void replacementRestoresSelection();
};

void RdbViewerModelsTest::checkModelUsesRequestedColumns() {
    rdb::CheckIndexDatabase index;
    rdb::CheckIndexEntry entry;
    entry.name = "M1.SPACING";
    entry.geometry_count = 7;
    index.checks.push_back(entry);

    CheckTableModel model;
    model.setIndex(index);

    QCOMPARE(model.columnCount(), 2);
    QCOMPARE(model.headerData(CheckTableModel::Name, Qt::Horizontal).toString(), QString("Check Name"));
    QCOMPARE(model.headerData(CheckTableModel::ResultCount, Qt::Horizontal).toString(), QString("Count"));
    QCOMPARE(model.index(0, CheckTableModel::ResultCount).data().toUInt(), 7U);
}

void RdbViewerModelsTest::coordinateResultUsesSingleColumn() {
    ResultTableModel model;
    DetailRow row;
    row.key = "10:p:1";
    row.coordinates = "(0, 0)  (10, 10)";
    model.appendRows(QVector<DetailRow>() << row);

    QCOMPARE(model.columnCount(), 1);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QString("Coords"));
    QCOMPARE(model.index(0, 0).data().toString(), row.coordinates);
    QCOMPARE(model.index(0, 0).data(Qt::ToolTipRole).toString(), row.coordinates);
}

void RdbViewerModelsTest::parameterResultCreatesKeyColumns() {
    ResultTableModel model;
    model.setMode(RdbViewerMode::AllParameters);
    DetailRow row;
    row.key = "10:p:1";
    row.resultLabel = "P 1";
    row.taggedValues["CN"] << "metal1" << "via";
    row.taggedValues["PP"] << "marker";
    model.appendRows(QVector<DetailRow>() << row);

    QCOMPARE(model.columnCount(), 3);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QString("Result"));
    QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QString("CN"));
    QCOMPARE(model.index(0, 1).data().toString(), QString("metal1\nvia"));
    QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QString("PP"));
}

void RdbViewerModelsTest::replacementRestoresSelection() {
    ResultTableModel model;
    QTableView view;
    view.setModel(&model);
    DetailRow first;
    first.key = "10:p:1";
    first.coordinates = "first";
    DetailRow second;
    second.key = "10:e:2";
    second.coordinates = "second";
    model.replaceRows(QVector<DetailRow>() << first << second);
    view.selectionModel()->select(model.index(1, 0),
                                  QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(model.index(1, 0), QItemSelectionModel::NoUpdate);
    const TableSelectionSnapshot snapshot = TableSelectionKeeper::capture(view, model);

    DetailRow replacement;
    replacement.key = "10:p:0";
    replacement.coordinates = "new";
    model.replaceRows(QVector<DetailRow>() << replacement << second);
    TableSelectionKeeper::restore(view, model, snapshot);

    QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(model.rowAt(view.currentIndex().row()).stableKey(), QString("10:e:2"));
}

QTEST_MAIN(RdbViewerModelsTest)
#include "rdb_viewer_models_test.moc"
