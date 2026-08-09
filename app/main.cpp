#include "mainwindow.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MainWindow App"));

    MainWindow window;
    window.show();

    return application.exec();
}
