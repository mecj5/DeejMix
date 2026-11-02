#include "mainwindow.h"
#include "audiomanager.h"
#include <QApplication>
#include <QDebug>

static int runSelfTest()
{
    // Headless verification of the Linux audio backend.
    qDebug() << "=== DeejMix audio self-test ===";
    AudioManager am(nullptr);
    const bool ok = am.initialize();
    qDebug() << "initialize() =>" << ok;
    if (!ok) return 1;

    const QStringList apps = am.getActiveAudioSessions();
    qDebug() << "active sessions:" << apps;

    am.setMasterVolume(0.65f, true);
    qDebug() << "setMasterVolume(0.65) issued";

    if (!apps.isEmpty()) {
        const QString app = apps.first();
        am.setAppVolume(app, 0.30f, true);
        qDebug() << "setAppVolume(" << app << ", 0.30) issued";
    }
    qDebug() << "=== self-test done ===";
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Setup application info for QSettings/Database
    QApplication::setOrganizationName("DeejMix");
    QApplication::setApplicationName("Deej Mix");

    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--selftest") return runSelfTest();
    }

    // Stylesheet is loaded and themed by MainWindow via applyAccentColor()

    MainWindow w;
    w.show();
    return a.exec();
}
