#include "calibre_text_dock.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

// 메인 창과 두 파일 열기 모드를 구성하고 Qt event loop를 실행한다.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Database RDB App"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Database RDB App"));
    window.resize(1440, 900);
    // macOS의 전역 메뉴바 사용 여부와 관계없이 테스트 창 안에서
    // File 메뉴를 항상 찾을 수 있게 한다. Qt 5.9에서도 지원된다.
    window.menuBar()->setNativeMenuBar(false);

    CalibreTextDock* dock = new CalibreTextDock(&window);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    QMenu* fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
    QAction* openCoords = fileMenu->addAction(
        QStringLiteral("Open RDB - Coordinates Only"));
    QAction* openAll = fileMenu->addAction(
        QStringLiteral("Open RDB - All Parameters"));

    // 선택한 모드로 파일 대화상자를 열고 새 파싱 작업을 시작한다.
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
