#include "rdb_viewer.hpp"

#include <QDockWidget>
#include <QHeaderView>
#include <QTableView>
#include <QTreeView>
#include <QtTest>

class RdbViewerIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void coordinatesModeOpensDockAndShowsCoordinates();
    void allParametersModeShowsTreeAndTaggedColumns();
    void reopeningCancelsOldWorkersAndDestructionJoinsThem();
};

void RdbViewerIntegrationTest::coordinatesModeOpensDockAndShowsCoordinates() {
    RdbViewer viewer;
    viewer.show();
    viewer.openFile(QStringLiteral(RDB_SAMPLE_DIR "/standard_sample.rdb"),
                    RdbViewerMode::CoordinatesOnly);

    QDockWidget* dock = viewer.findChild<QDockWidget*>(QStringLiteral("rdbResultsDock"));
    QTableView* checkView = viewer.findChild<QTableView*>(QStringLiteral("checkTable"));
    QTableView* detailView = viewer.findChild<QTableView*>(QStringLiteral("detailTable"));
    QVERIFY(dock != 0);
    QVERIFY(checkView != 0);
    QVERIFY(detailView != 0);
    QVERIFY(dock->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(checkView->model()->rowCount(), 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(checkView->currentIndex().isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(detailView->model()->rowCount() > 0, 5000);
    QCOMPARE(detailView->model()->headerData(0, Qt::Horizontal).toString(), QString("Coords"));
}

void RdbViewerIntegrationTest::allParametersModeShowsTreeAndTaggedColumns() {
    RdbViewer viewer;
    viewer.openFile(QStringLiteral(RDB_SAMPLE_DIR "/standard_sample.rdb"),
                    RdbViewerMode::AllParameters);

    QTreeView* treeView = viewer.findChild<QTreeView*>(QStringLiteral("checkTree"));
    QTableView* detailView = viewer.findChild<QTableView*>(QStringLiteral("detailTable"));
    QVERIFY(treeView != 0);
    QVERIFY(detailView != 0);
    QTRY_VERIFY_WITH_TIMEOUT(treeView->model()->rowCount() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(treeView->currentIndex().isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(detailView->model()->rowCount() > 0, 5000);
    QCOMPARE(treeView->model()->headerData(0, Qt::Horizontal).toString(), QString("Check Name"));
    QCOMPARE(treeView->model()->headerData(1, Qt::Horizontal).toString(), QString("Count"));
    QVERIFY(detailView->model()->columnCount() >= 2);
}

void RdbViewerIntegrationTest::reopeningCancelsOldWorkersAndDestructionJoinsThem() {
    RdbViewer* viewer = new RdbViewer;
    viewer->openFile(QStringLiteral(RDB_SAMPLE_DIR "/large_standard_sample.rdb"),
                     RdbViewerMode::CoordinatesOnly);
    viewer->openFile(QStringLiteral(RDB_SAMPLE_DIR "/standard_sample.rdb"),
                     RdbViewerMode::CoordinatesOnly);

    QTableView* checkView = viewer->findChild<QTableView*>(QStringLiteral("checkTable"));
    QTableView* detailView = viewer->findChild<QTableView*>(QStringLiteral("detailTable"));
    QVERIFY(checkView != 0);
    QVERIFY(detailView != 0);
    QTRY_COMPARE_WITH_TIMEOUT(checkView->model()->rowCount(), 3, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(detailView->model()->rowCount(), 2, 5000);
    delete viewer;
}

QTEST_MAIN(RdbViewerIntegrationTest)
#include "rdb_viewer_integration_test.moc"
