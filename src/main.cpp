#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("NikonTether");
    app.setOrganizationName("NikonTether");

    MainWindow window;
    window.show();

    return app.exec();
}
