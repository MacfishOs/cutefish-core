#include <QGuiApplication>
#include <QTimer>

#include "../clipboard.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    Clipboard clipboard;
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    return app.exec();
}
