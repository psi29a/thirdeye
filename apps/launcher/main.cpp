#include <QApplication>
#include <QIcon>

#include "MainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Mindwerks");
    QCoreApplication::setOrganizationDomain("mindwerks.net");
    QCoreApplication::setApplicationName("Thirdeye");

    // Set the taskbar/dock/window icon. QApplication-level wins over the
    // parent .app bundle's Info.plist, so the launcher shows its own icon
    // even though it lives inside thirdeye.app.
    QApplication::setWindowIcon(QIcon(":/thirdeye.png"));

    MainWindow w;
    w.show();
    return app.exec();
}
