#include "calibre_text_dock.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Original RDB App"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Original RDB App"));
    window.resize(1440, 900);

    CalibreTextDock* dock = new CalibreTextDock(&window);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    QMenu* fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
    QAction* openCoords = fileMenu->addAction(
        QStringLiteral("Open RDB - Coordinates Only"));
    QAction* openAll = fileMenu->addAction(
        QStringLiteral("Open RDB - All Parameters"));

    const std::function<void(CalibreTextDock::RDB_TYPE)> openFile =
        [&window, dock](CalibreTextDock::RDB_TYPE type) {
            const QString path = QFileDialog::getOpenFileName(
                &window,
                QStringLiteral("Open RDB File"),
                QString(),
                QStringLiteral("RDB Files (*.rdb);;All Files (*)"));
            if (path.isEmpty()) return;
            dock->SetType(type);
            dock->ParseRDBCheck(path);
        };
    QObject::connect(openCoords, &QAction::triggered,
                     [&openFile]() { openFile(CalibreTextDock::COORDS_ONLY); });
    QObject::connect(openAll, &QAction::triggered,
                     [&openFile]() { openFile(CalibreTextDock::ALL_PARAMS); });

    window.show();
    if (argc > 1) {
        dock->ParseRDBCheck(QString::fromLocal8Bit(argv[1]));
    }
    return application.exec();
}
