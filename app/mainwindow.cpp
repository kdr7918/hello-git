#include "mainwindow.hpp"

#include "calibre_text_dock_table.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      calibre_text_dock_(new CalibreTextDockTable(this)) {
    setWindowTitle(tr("MainWindow App"));
    resize(1440, 900);

    QLabel* welcome = new QLabel(tr("MainWindow App"), this);
    welcome->setAlignment(Qt::AlignCenter);
    setCentralWidget(welcome);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openAllParams =
        fileMenu->addAction(tr("Open File (.rdb) - All Params"));
    openAllParams->setObjectName(QStringLiteral("OpenAllParamsAction"));
    QAction* openCoordsOnly =
        fileMenu->addAction(tr("Open File (.rdb) - Coords Only"));
    openCoordsOnly->setObjectName(QStringLiteral("OpenCoordsOnlyAction"));
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    connect(openAllParams, &QAction::triggered,
            this, [this]() { ChooseAndLoadRdbIndex(true); });
    connect(openCoordsOnly, &QAction::triggered,
            this, [this]() { ChooseAndLoadRdbIndex(false); });
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    addDockWidget(Qt::BottomDockWidgetArea, calibre_text_dock_);
    resizeDocks(
        QList<QDockWidget*>() << calibre_text_dock_,
        QList<int>() << 450,
        Qt::Vertical);

    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::ChooseAndLoadRdbIndex(bool allParameters) {
    // 파일 선택 이후의 파싱, 진행률, 오류 처리는 DockTable이 담당한다.
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open RDB File"),
        QString(),
        tr("RDB Files (*.rdb);;All Files (*)"));
    if (path.isEmpty()) return;

    calibre_text_dock_->LoadRdbIndex(path, allParameters);
}
