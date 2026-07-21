#include "main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("large-file-viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("hello-git"));

    MainWindow window;
    window.show();
    if (application.arguments().size() > 1)
        window.loadFile(application.arguments().at(1));
    return application.exec();
}
