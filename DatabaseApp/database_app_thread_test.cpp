#include "calibre_text_dock.hpp"
#include "rdb_check_index.hpp"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QMenu>
#include <QTimer>
#include <QTreeView>

#include <iostream>

#ifndef RDB_SAMPLE_FILE
#define RDB_SAMPLE_FILE ""
#endif

#ifndef RDB_SECOND_SAMPLE_FILE
#define RDB_SECOND_SAMPLE_FILE RDB_SAMPLE_FILE
#endif

namespace {

// menu의 직접 자식 중 표시 이름이 같은 submenu를 찾는다.
QMenu* FindDirectSubmenu(QMenu* menu, const QString& title) {
    if (!menu) return 0;
    const QList<QAction*> actions = menu->actions();
    for (int i = 0; i < actions.size(); ++i) {
        if (actions[i]->menu() && actions[i]->text() == title) {
            return actions[i]->menu();
        }
    }
    return 0;
}

// menu의 직접 자식 중 지정된 grouping 순서를 가진 적용 action을 찾는다.
QAction* FindDirectGroupingAction(
    QMenu* menu,
    const QStringList& keys) {
    if (!menu) return 0;
    const QList<QAction*> actions = menu->actions();
    for (int i = 0; i < actions.size(); ++i) {
        if (!actions[i]->menu() &&
            actions[i]->data().toStringList() == keys) {
            return actions[i];
        }
    }
    return 0;
}

// EL > Check Name > PA 경로와 실제 TreeModel 적용 순서를 함께 검증한다.
bool VerifyOrderedGroupingMenu(CalibreTextDock& dock) {
    QMenu* const root = dock.findChild<QMenu*>(
        QStringLiteral("tree_grouping_menu"));
    RDBTreeModel* const treeModel = dock.findChild<RDBTreeModel*>();
    QMenu* const elMenu = FindDirectSubmenu(
        root, QStringLiteral("EL"));
    if (elMenu) {
        QMetaObject::invokeMethod(
            elMenu, "aboutToShow", Qt::DirectConnection);
    }
    QMenu* const checkNameMenu = FindDirectSubmenu(
        elMenu, QStringLiteral("Check Name"));
    if (checkNameMenu) {
        QMetaObject::invokeMethod(
            checkNameMenu, "aboutToShow", Qt::DirectConnection);
    }

    const QStringList oneKey = QStringList()
        << QStringLiteral("EL");
    const QStringList twoKeys = QStringList()
        << QStringLiteral("EL")
        << QStringLiteral("Check Name");
    const QStringList threeKeys = QStringList()
        << QStringLiteral("EL")
        << QStringLiteral("Check Name")
        << QStringLiteral("PA");
    if (!treeModel ||
        !FindDirectGroupingAction(elMenu, oneKey) ||
        !FindDirectGroupingAction(checkNameMenu, twoKeys)) {
        return false;
    }
    QAction* const paAction = FindDirectGroupingAction(
        checkNameMenu, threeKeys);
    if (!paAction) return false;

    paAction->trigger();
    return treeModel->GetCompKey() == threeKeys &&
        treeModel->rowCount() > 0 &&
        treeModel->rowCount(treeModel->index(0, RDBTreeModel::KEY)) > 0;
}

} // namespace

// 빠른 연속 재파싱과 BG 완료 후 GUI 정책을 event loop에서 검증한다.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const QString sample = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QString::fromLocal8Bit(RDB_SAMPLE_FILE);
    const QString secondSample = argc > 2
        ? QString::fromLocal8Bit(argv[2])
        : QString::fromLocal8Bit(RDB_SECOND_SAMPLE_FILE);
    if (sample.isEmpty() || secondSample.isEmpty()) {
        std::cerr << "sample RDB path is empty\n";
        return 1;
    }
    const QByteArray encodedSecondSample = QFile::encodeName(secondSample);
    const std::size_t expectedCheckCount =
        rdb::FastCheckIndexParser().parse_database(
            encodedSecondSample.constData()).checks.size();

    CalibreTextDock dock;
    QTreeView* const treeView =
        dock.findChild<QTreeView*>(QStringLiteral("rdb_tree_view"));
    if (!treeView ||
        treeView->header()->contextMenuPolicy() != Qt::NoContextMenu) {
        std::cerr << "Tree grouping must be blocked before background parsing\n";
        return 1;
    }
    bool failed = false;
    bool completed = false;
    int lastProgress = -1;
    QTimer completionPoll;
    completionPoll.setInterval(10);

    QObject::connect(
        &dock, &CalibreTextDock::ParsingFailed,
        [&application, &failed](const QString& message) {
            failed = true;
            std::cerr << message.toLocal8Bit().constData() << '\n';
            application.exit(1);
        });
    QObject::connect(
        &completionPoll, &QTimer::timeout,
        [&application, &dock, treeView, expectedCheckCount,
         &completed, &failed]() {
            const QList<RDBModel*> models = dock.findChildren<RDBModel*>();
            for (int i = 0; i < models.size(); ++i) {
                if (models[i]->GetType() != RDBModel::CHIP_TABLE) continue;
                const RDB_DATABASE_PTR database = models[i]->GetDatabase();
                if (database &&
                    database->check_count() == expectedCheckCount &&
                    database->loaded_check_count() == database->check_count()) {
                    if (treeView->header()->contextMenuPolicy() !=
                            Qt::CustomContextMenu) {
                        failed = true;
                        std::cerr <<
                            "Tree grouping was not enabled after background parsing\n";
                        application.exit(1);
                        return;
                    }
                    if (!VerifyOrderedGroupingMenu(dock)) {
                        failed = true;
                        std::cerr <<
                            "Ordered Tree grouping menu is incorrect\n";
                        application.exit(1);
                        return;
                    }
                    completed = true;
                    application.quit();
                }
                break;
            }
        });
    QTimer::singleShot(
        10000, &application,
        [&application, &failed]() {
            failed = true;
            std::cerr << "DatabaseApp parser thread test timed out\n";
            application.exit(1);
        });

    // 첫 작업의 queued signal이 다음 파일 상태를 덮어쓰지 않는지 함께 검증한다.
    dock.ParseRDBCheck(sample);
    QTimer::singleShot(0, &dock, [&dock, secondSample, &lastProgress]() {
        dock.ParseRDBCheck(secondSample, [&lastProgress](int value) {
            lastProgress = value;
        });
    });
    completionPoll.start();
    const int result = application.exec();
    if (result != 0 || failed || !completed || lastProgress != 100) {
        return 1;
    }
    return 0;
}
