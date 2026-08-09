#include "calibre_text_dock_table.hpp"
#include "rdb_tree_model.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QModelIndex>
#include <QPushButton>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QTreeView>
#include <QVector>
#include <QTableView>

#include <functional>
#include <iostream>

namespace {

bool WaitFor(const std::function<bool()>& condition, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (condition()) return true;
        QThread::msleep(1);
    }
    return condition();
}

int FindColumn(const QAbstractItemModel* model, const QString& header) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(
                column, Qt::Horizontal, Qt::DisplayRole).toString() == header) {
            return column;
        }
    }
    return -1;
}

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << std::endl;
    return condition;
}

bool WriteBatchSample(QTemporaryFile& file) {
    if (!file.open()) return false;

    QTextStream output(&file);
    output << "TOP_CHIP 1000\n"
           << "BIG.CHECK\n"
           << "10001 10001 0 Jul 21 10:32:45 2026\n";
    for (int result = 1; result <= 10001; ++result) {
        output << "p " << result << " 1\n"
               << result << ' ' << result + 1 << '\n';
    }
    output << "SMALL.CHECK\n"
           << "1 1 0 Jul 21 10:32:46 2026\n"
           << "p 1 1\n"
           << "999 1000\n"
           << "CANCEL.CHECK\n"
           << "10001 10001 0 Jul 21 10:32:47 2026\n";
    for (int result = 1; result <= 10001; ++result) {
        output << "p " << result << " 1\n"
               << result + 20000 << ' ' << result + 20001 << '\n';
    }
    output.flush();
    const bool succeeded = output.status() == QTextStream::Ok;
    file.close();
    return succeeded;
}

bool WriteDuplicateTreeSample(QTemporaryFile& file) {
    if (!file.open()) return false;

    QTextStream output(&file);
    output << "TOP_CHIP 1000\n"
           << "DUP.CHECK\n"
           << "2 2 0 Jul 21 10:32:45 2026\n"
           << "p 1 1\n1 1\n"
           << "CN CELL_A\n"
           << "p 2 1\n2 2\n"
           << "CN CELL_B\n"
           << "DUP.CHECK\n"
           << "1 1 0 Jul 21 10:32:46 2026\n"
           << "p 1 1\n3 3\n"
           << "CN CELL_A\n";
    output.flush();
    const bool succeeded = output.status() == QTextStream::Ok;
    file.close();
    return succeeded;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    CalibreTextDockTable dock;

    QTableView* checkTable = dock.findChild<QTableView*>(
        QStringLiteral("CheckTable"));
    QTableView* detailTable = dock.findChild<QTableView*>(
        QStringLiteral("CoordsTable"));
    QTreeView* checkTree = dock.findChild<QTreeView*>(
        QStringLiteral("CheckTreeView"));
    QLineEdit* treeSearchEdit = dock.findChild<QLineEdit*>(
        QStringLiteral("TreeSearchEdit"));
    QPushButton* treeSearchButton = dock.findChild<QPushButton*>(
        QStringLiteral("TreeSearchButton"));
    QPushButton* treeSearchPreviousButton = dock.findChild<QPushButton*>(
        QStringLiteral("TreeSearchPreviousButton"));
    QPushButton* treeSearchNextButton = dock.findChild<QPushButton*>(
        QStringLiteral("TreeSearchNextButton"));
    if (!Expect(checkTable && checkTree && detailTable && treeSearchEdit &&
                    treeSearchButton && treeSearchPreviousButton &&
                    treeSearchNextButton,
                "Dock check/detail/search views were not created")) {
        return 1;
    }
    if (!Expect(
            checkTable->verticalHeader()->defaultSectionSize() >= 42 &&
                detailTable->verticalHeader()->defaultSectionSize() >= 42,
            "Table row height minimum was not applied")) {
        return 1;
    }

    if (argc == 2) {
        const std::size_t checkCount = dock.LoadRdbIndex(
            QString::fromLocal8Bit(argv[1]),
            RdbTableModel::AllParameters);
        if (!Expect(checkCount >= 5,
                    "Large RDB sample has fewer than five checks") ||
            !Expect(
                WaitFor([detailTable]() {
                    return detailTable->model()->rowCount() == 200000;
                }, 30000),
                "Large RDB first detail did not finish")) {
            return 1;
        }

        QAbstractItemModel* model = detailTable->model();
        RdbTableModel* rdbModel = static_cast<RdbTableModel*>(model);
        const int ppColumn = FindColumn(model, QStringLiteral("PP"));
        const int cnColumn = FindColumn(model, QStringLiteral("CN"));
        if (!Expect(ppColumn >= 0 && cnColumn >= 0,
                    "Large RDB tag headers were not created") ||
            !Expect(
                model->data(model->index(0, ppColumn)).toString() ==
                    QStringLiteral("stress marker check 1 result 1"),
                "Large RDB first row contains shifted or duplicate data") ||
            !Expect(
                model->data(model->index(199999, ppColumn)).toString() ==
                    QStringLiteral("stress marker check 1 result 200000"),
                "Large RDB last row is empty or shifted") ||
            !Expect(
                model->data(model->index(199999, cnColumn)).toString() ==
                    QStringLiteral("CELL_1_3392"),
                "Large RDB last CN CellName is incorrect") ||
            !Expect(
                rdbModel->GetDatabase()->check(0U).detail_loaded &&
                    rdbModel->GetDatabase()->check(0U).results.count == 200000U &&
                    rdbModel->GetDatabase()->results.size() >= 200000U &&
                    rdbModel->GetDatabase()->tagged_values.size() >= 600000U &&
                    rdbModel->GetDatabase()->vertices.size() >= 800000U,
                "Large detail was not stored in canonical Database pools")) {
            return 1;
        }

        std::cout << "Large RDB UI verification passed" << std::endl;
        return 0;
    }

    if (!Expect(
            dock.LoadRdbIndex(
                QString::fromUtf8(RDB_SAMPLE_FILE),
                RdbTableModel::AllParameters) == 3,
            "Standard sample check index did not contain three rows")) {
        return 1;
    }

    if (!Expect(checkTree->currentIndex().row() == 0,
                "The first Check tree node was not selected automatically") ||
        !Expect(checkTree->model()->columnCount() == 2,
                "All Params tree did not expose Key and Count columns") ||
        !Expect(
            checkTree->model()->headerData(
                0, Qt::Horizontal, Qt::DisplayRole).toString() ==
                QStringLiteral("Key") &&
            checkTree->model()->headerData(
                1, Qt::Horizontal, Qt::DisplayRole).toString() ==
                QStringLiteral("Count"),
            "All Params tree headers are incorrect") ||
        !Expect(
            WaitFor([detailTable]() {
                return detailTable->model()->rowCount() == 2;
            }),
            "All Params detail rows were not loaded")) {
        return 1;
    }

    QAbstractItemModel* allParamsModel = detailTable->model();
    const int ppColumn = FindColumn(allParamsModel, QStringLiteral("PP"));
    const int cnColumn = FindColumn(allParamsModel, QStringLiteral("CN"));
    if (!Expect(ppColumn >= 0 && cnColumn >= 0,
                "Tagged-value keys were not exposed as headers") ||
        !Expect(
            allParamsModel->data(
                allParamsModel->index(0, ppColumn)).toString() ==
                QStringLiteral("M1 spacing marker"),
            "Tagged-value payload was not exposed as cell data") ||
        !Expect(
            allParamsModel->data(
                allParamsModel->index(0, cnColumn)).toString() ==
                QStringLiteral("INV_X1"),
            "CN payload was not reduced to its CellName token")) {
        return 1;
    }

    RdbTableModel* cachedModel =
        static_cast<RdbTableModel*>(detailTable->model());
    const std::shared_ptr<rdb::Database> firstCheckDatabase =
        cachedModel->GetDatabase();
    if (!Expect(
            WaitFor([firstCheckDatabase]() {
                return firstCheckDatabase->loaded_rule_check_count ==
                    firstCheckDatabase->check_count();
            }),
            "Background parser did not preload every standard-sample Check") ||
        !Expect(firstCheckDatabase->results.size() == 3U &&
                    firstCheckDatabase->tagged_values.size() == 7U &&
                    firstCheckDatabase->vertices.size() == 8U &&
                    firstCheckDatabase->edges.size() == 2U &&
                    firstCheckDatabase->check(0U).results.count == 2U,
                "Table model data was not stored in rdb::Database pools")) {
        return 1;
    }
    if (!Expect(
            WaitFor([checkTree]() {
                return checkTree->header()->contextMenuPolicy() ==
                        Qt::CustomContextMenu &&
                    checkTree->header()->property("GroupingReady").toBool();
            }),
            "Tree header grouping menu was not enabled after background parsing")) {
        return 1;
    }
    RdbTreeModel* treeModel =
        static_cast<RdbTreeModel*>(checkTree->model());
    if (!Expect(
            treeModel->GetAvailableCategories().contains(
                QStringLiteral("CN")),
            "Tagged Value keys were not exposed as tree grouping categories")) {
        return 1;
    }

    checkTree->setCurrentIndex(checkTree->model()->index(1, 0));
    checkTree->setCurrentIndex(checkTree->model()->index(0, 0));
    if (!Expect(
            cachedModel->GetDatabase() == firstCheckDatabase &&
                firstCheckDatabase->check(0U).detail_loaded &&
                cachedModel->rowCount() == 2,
            "Completed detail data was not reused from cache")) {
        return 1;
    }
    if (!Expect(cachedModel->TotalRowCount() == 2U,
                "STL detail storage reported an incorrect total row count")) {
        return 1;
    }
    cachedModel->SetRowOffset(1U);
    if (!Expect(cachedModel->RowOffset() == 1U &&
                    cachedModel->rowCount() == 1,
                "Qt-visible row window did not move over STL storage")) {
        return 1;
    }
    cachedModel->SetRowOffset(0U);

    if (!Expect(
            dock.LoadRdbIndex(
                QString::fromUtf8(RDB_SAMPLE_FILE),
                RdbTableModel::CoordinatesOnly) == 3,
            "Coords Only index reload failed")) {
        return 1;
    }
    if (!Expect(checkTable->currentIndex().row() == 0,
                "The first check was not reselected after mode change") ||
        !Expect(
            WaitFor([detailTable]() {
                return detailTable->model()->rowCount() == 2;
            }),
            "Coords Only detail rows were not loaded") ||
        !Expect(detailTable->model()->columnCount() == 1,
                "Coords Only model did not expose exactly one column") ||
        !Expect(
            detailTable->model()->data(
                detailTable->model()->index(0, 0)).toString() ==
                QStringLiteral(
                    "10000 20000 14000 20000 14000 23000 10000 23000"),
            "Polygon coordinates were not flattened into one cell") ||
        !Expect(
            detailTable->model()->data(
                detailTable->model()->index(1, 0)).toString() ==
                QStringLiteral(
                    "30000 20000 33000 20000 33000 20000 33000 23000"),
            "Edge coordinates were not flattened into one cell")) {
        return 1;
    }
    if (!Expect(firstCheckDatabase->results.size() == 3U,
                "Changing detail mode duplicated cached Database results")) {
        return 1;
    }
    if (!Expect(
            detailTable->horizontalHeader()->sectionResizeMode(0) ==
                QHeaderView::Stretch,
            "Detail columns were not fitted to the view")) {
        return 1;
    }

    QTemporaryFile batchFile;
    if (!Expect(WriteBatchSample(batchFile),
                "Could not create batch parser sample")) {
        return 1;
    }
    if (!Expect(
            dock.LoadRdbIndex(
                batchFile.fileName(),
                RdbTableModel::CoordinatesOnly) == 3,
            "Batch sample check index did not contain three rows")) {
        return 1;
    }

    QVector<int> insertedBatchSizes;
    QObject::connect(
        detailTable->model(),
        &QAbstractItemModel::rowsInserted,
        [&insertedBatchSizes](const QModelIndex&, int first, int last) {
            insertedBatchSizes.append(last - first + 1);
        });
    if (!Expect(
            WaitFor([detailTable]() {
                return detailTable->model()->rowCount() == 10001;
            }, 10000),
            "Large detail result did not finish") ||
        !Expect(
            insertedBatchSizes == (QVector<int>() << 10000 << 1),
            "Detail table was not updated in 10,000-row batches")) {
        return 1;
    }

    checkTable->setCurrentIndex(checkTable->model()->index(1, 0));
    if (!Expect(
            WaitFor([detailTable]() {
                return detailTable->model()->rowCount() == 1;
            }),
            "Selected Check did not update during background preloading")) {
        return 1;
    }
    RdbTableModel* batchModel =
        static_cast<RdbTableModel*>(detailTable->model());
    if (!Expect(
            WaitFor([batchModel]() {
                return batchModel->GetDatabase()->loaded_rule_check_count ==
                    batchModel->GetDatabase()->check_count();
            }, 10000),
            "Background parser did not preload every batch-sample Check")) {
        return 1;
    }
    const std::size_t completedPoolSize =
        batchModel->GetDatabase()->results.size();
    checkTable->setCurrentIndex(checkTable->model()->index(2, 0));
    if (!Expect(detailTable->model()->rowCount() == 10001,
                "Preloaded third Check was not restored from cache")) {
        return 1;
    }
    checkTable->setCurrentIndex(checkTable->model()->index(1, 0));
    if (!Expect(
            detailTable->model()->rowCount() == 1 &&
                detailTable->model()->data(
                    detailTable->model()->index(0, 0)).toString() ==
                    QStringLiteral("999 1000") &&
                batchModel->GetDatabase()->results.size() == completedPoolSize &&
                completedPoolSize == 20003U &&
                batchModel->GetDatabase()->check(2U).detail_loaded,
            "Selecting a preloaded Check reparsed or changed cached data")) {
        return 1;
    }

    QTemporaryFile duplicateTreeFile;
    if (!Expect(WriteDuplicateTreeSample(duplicateTreeFile),
                "Could not create duplicate Check tree sample") ||
        !Expect(
            dock.LoadRdbIndex(
                duplicateTreeFile.fileName(),
                RdbTableModel::AllParameters) == 2,
            "Duplicate Check tree index did not contain two Checks")) {
        return 1;
    }
    if (!Expect(checkTree->model()->rowCount() == 1,
                "Duplicate Check names were not merged into one tree node") ||
        !Expect(
            checkTree->model()->data(
                checkTree->model()->index(0, 0)).toString() ==
                QStringLiteral("DUP.CHECK") &&
            checkTree->model()->data(
                checkTree->model()->index(0, 1)).toULongLong() == 3U,
            "Merged Check tree node has an incorrect key or Result count") ||
        !Expect(
            WaitFor([detailTable]() {
                return detailTable->model()->rowCount() == 3;
            }),
            "Selecting a merged Check node did not show all Check results") ||
        !Expect(
            WaitFor([checkTree]() {
                return checkTree->header()->property(
                    "GroupingReady").toBool();
            }),
            "Duplicate Check tree did not enable grouping after parsing")) {
        return 1;
    }

    treeModel = static_cast<RdbTreeModel*>(checkTree->model());
    treeModel->SetGroupingCategories(
        QStringList() << RdbTreeModel::CheckNameCategory()
                      << QStringLiteral("CN"));
    const QModelIndex duplicateRoot = treeModel->index(0, 0);
    if (!Expect(treeModel->rowCount(duplicateRoot) == 2,
                "Tagged Value grouping did not create two CN child nodes") ||
        !Expect(
            treeModel->data(
                treeModel->index(0, 1, duplicateRoot)).toULongLong() == 2U &&
            treeModel->data(
                treeModel->index(1, 1, duplicateRoot)).toULongLong() == 1U,
            "Tagged Value grouping Result counts are incorrect")) {
        return 1;
    }
    checkTree->setCurrentIndex(treeModel->index(0, 0, duplicateRoot));
    if (!Expect(detailTable->model()->rowCount() == 2,
                "Tagged Value child selection did not filter exact results")) {
        return 1;
    }

    treeSearchEdit->setText(QStringLiteral("cell"));
    treeSearchButton->click();
    if (!Expect(
            checkTree->currentIndex().data().toString() ==
                    QStringLiteral("CELL_A") &&
                detailTable->model()->rowCount() == 2,
            "BFS tree search did not select the first matching row") ||
        !Expect(treeSearchPreviousButton->isEnabled() &&
                    treeSearchNextButton->isEnabled(),
                "Tree search navigation buttons were not enabled")) {
        return 1;
    }
    treeSearchNextButton->click();
    if (!Expect(
            checkTree->currentIndex().data().toString() ==
                    QStringLiteral("CELL_B") &&
                detailTable->model()->rowCount() == 1,
            "Next tree search result was not selected")) {
        return 1;
    }
    treeSearchNextButton->click();
    if (!Expect(
            checkTree->currentIndex().data().toString() ==
                QStringLiteral("CELL_A"),
            "Tree search next navigation did not wrap")) {
        return 1;
    }
    treeSearchPreviousButton->click();
    if (!Expect(
            checkTree->currentIndex().data().toString() ==
                QStringLiteral("CELL_B"),
            "Tree search previous navigation did not wrap")) {
        return 1;
    }

    std::cout << "RDB UI smoke test passed" << std::endl;
    return 0;
}
