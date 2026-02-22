#include <QApplication>
#include <QCoreApplication>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SimpleEDTerraform");
    QCoreApplication::setApplicationName("SimpleEDTerraform");

    MainWindow window;
    window.show();

    return app.exec();
}
