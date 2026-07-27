#include "rdb_viewer.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    RdbViewer viewer;
    viewer.resize(1200, 760);
    viewer.show();

    QString path;
    if (argc > 1) path = QString::fromLocal8Bit(argv[1]);
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(&viewer, "Open ASCII RDB", QString(),
                                            "ASCII RDB (*.rdb);;All files (*)");
    }
    if (!path.isEmpty()) viewer.openFile(path);
    return application.exec();
}
