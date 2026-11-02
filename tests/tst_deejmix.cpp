#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "channelstrip.h"
#include "databasemanager.h"
#include "hardwaremanager.h"
#include "mainwindow.h"

class DeejMixTests : public QObject
{
    Q_OBJECT

private slots:
    void assignmentSurvivesStreamDisappearance()
    {
        ChannelStrip strip(0);
        strip.populateApps({"Music", "Browser"});
        strip.setAssignedApp("Music");
        QSignalSpy changes(&strip, &ChannelStrip::appAssignmentChanged);
        auto *dropdown = strip.findChild<QComboBox *>();
        QVERIFY(dropdown);

        for (int i = 0; i < 3; ++i) {
            strip.populateApps({"Browser"});
            QCOMPARE(strip.getAssignedApp(), QString("Music"));
            QCOMPARE(dropdown->currentText(), QString("Music"));
            QVERIFY(!dropdown->currentData(Qt::ToolTipRole).toString().isEmpty());
        }

        strip.populateApps({"Browser", "Music"});
        QCOMPARE(strip.getAssignedApp(), QString("Music"));
        QCOMPARE(dropdown->count(), 5); // Placeholder, two devices, two apps.
        QVERIFY(dropdown->currentData(Qt::ToolTipRole).toString().isEmpty());
        QCOMPARE(changes.count(), 0);
    }

    void assignmentSurvivesRestartWhileAppIsAbsent()
    {
        {
            DatabaseManager db;
            QVERIFY(db.initializeDatabase());
            ChannelStrip strip(2);
            strip.populateApps({"Music"});
            connect(&strip, &ChannelStrip::appAssignmentChanged,
                    &db, &DatabaseManager::setChannelApp);
            auto *dropdown = strip.findChild<QComboBox *>();
            dropdown->setCurrentText("Music");
            QCOMPARE(db.getChannelApp(2), QString("Music"));
            strip.populateApps({});
            QCOMPARE(db.getChannelApp(2), QString("Music"));
        }
        {
            DatabaseManager db;
            QVERIFY(db.initializeDatabase());
            ChannelStrip strip(2);
            strip.populateApps({});
            strip.setAssignedApp(db.getChannelApp(2));
            strip.populateApps({});
            QCOMPARE(strip.getAssignedApp(), QString("Music"));
            strip.populateApps({"Music"});
            QCOMPARE(strip.getAssignedApp(), QString("Music"));

            connect(&strip, &ChannelStrip::appAssignmentChanged,
                    &db, &DatabaseManager::setChannelApp);
            strip.findChild<QComboBox *>()->setCurrentIndex(0);
            QVERIFY(db.getChannelApp(2).isEmpty());
            strip.populateApps({"Music"});
            QVERIFY(strip.getAssignedApp().isEmpty());
        }
    }

    void selectorAvailableInBothModes()
    {
        ChannelStrip strip(0);
        strip.populateApps({"Music", "Browser"});
        strip.setAssignedApp("Music");
        strip.show();
        auto *dropdown = strip.findChild<QComboBox *>();
        QVERIFY(dropdown);
        for (bool knobs : {true, false, true}) {
            strip.setVisualMode(knobs);
            QVERIFY(dropdown->isVisible());
            QCOMPARE(strip.getAssignedApp(), QString("Music"));
        }
        dropdown->setCurrentText("Browser");
        QCOMPARE(strip.getAssignedApp(), QString("Browser"));
    }

    void hardwareDestructionDoesNotEmitDisconnect()
    {
        auto *hardware = new HardwareManager;
        QSignalSpy disconnected(hardware, &HardwareManager::hardwareDisconnected);
        delete hardware;
        QCOMPARE(disconnected.count(), 0);
    }

    void stoppedHardwareIgnoresPendingScans()
    {
        HardwareManager hardware;
        auto *serial = hardware.findChild<QSerialPort *>();
        QVERIFY(serial);
        QSignalSpy errors(serial, &QSerialPort::errorOccurred);
        hardware.startAutoDetect();
        hardware.disconnectDevice();
        QCoreApplication::processEvents();
        QVERIFY(!serial->isOpen());
        QCOMPARE(errors.count(), 0);
    }

    void windowQuitsCleanly()
    {
        MainWindow window;
        // Do not probe physical hardware during the event-loop check.
        window.findChild<HardwareManager *>()->disconnectDevice();
        QTimer::singleShot(0, &window, [&window] {
            QMetaObject::invokeMethod(&window, "trayQuit");
        });
        QCOMPARE(qApp->exec(), 0);
        // Destruction used to emit hardwareDisconnected into a destroyed UI.
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir data;
    if (!data.isValid()) return 1;
    // Keep tests away from the user's saved assignments and audio server.
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    qputenv("APPDATA", data.path().toUtf8());
    qputenv("PULSE_SERVER", "unix:/nonexistent/deejmix-test-pulse");
    QApplication app(argc, argv);
    app.setOrganizationName("DeejMixTests");
    app.setApplicationName("DeejMixTests");
    DeejMixTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_deejmix.moc"
