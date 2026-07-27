#include "rdb_viewer.hpp"

#include <QTableView>
#include <QtTest>

class RdbViewerIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void opensIndexSelectsFirstAndShowsDetails();
    void reopeningCancelsOldWorkersAndDestructionJoinsThem();
};

void RdbViewerIntegrationTest::opensIndexSelectsFirstAndShowsDetails() {
    RdbViewer viewer;
    viewer.openFile(QStringLiteral(RDB_SAMPLE_DIR "/standard_sample.rdb"));

    QTableView* checkView = viewer.findChild<QTableView*>(QStringLiteral("checkTable"));
    QTableView* detailView = viewer.findChild<QTableView*>(QStringLiteral("detailTable"));
    QVERIFY(checkView != nullptr);
    QVERIFY(detailView != nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(checkView->model()->rowCount(), 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(checkView->currentIndex().isValid(), 5000);
    QCOMPARE(checkView->currentIndex().row(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(detailView->model()->rowCount(), 2, 5000);
}

void RdbViewerIntegrationTest::reopeningCancelsOldWorkersAndDestructionJoinsThem() {
    RdbViewer* viewer = new RdbViewer;
    viewer->openFile(QStringLiteral(RDB_SAMPLE_DIR "/large_standard_sample.rdb"));
    viewer->openFile(QStringLiteral(RDB_SAMPLE_DIR "/standard_sample.rdb"));

    QTableView* checkView = viewer->findChild<QTableView*>(QStringLiteral("checkTable"));
    QTableView* detailView = viewer->findChild<QTableView*>(QStringLiteral("detailTable"));
    QVERIFY(checkView != nullptr);
    QVERIFY(detailView != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(checkView->model()->rowCount(), 3, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(detailView->model()->rowCount(), 2, 5000);

    // worker callback과 QObject 파괴가 경합해도 destructor가 cancellation 후 join한다.
    delete viewer;
}

QTEST_MAIN(RdbViewerIntegrationTest)
#include "rdb_viewer_integration_test.moc"
