#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QColor>
#include "hardwaremanager.h"
#include "channelstrip.h"
#include "audiomanager.h"
#include "databasemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct AccentPreset {
    QString name;
    QString accent;
    QString accentDark;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onHardwareConnected(const QString& portName);
    void onHardwareDisconnected();
    void onSliderValuesReceived(const QVector<int>& values);
    void checkHardwareConnection();
    void refreshAudioSessions();
    void refreshDevices();
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void trayQuit();
    void onAppAssignmentChanged(int channelId, const QString& appName);

private:
    Ui::MainWindow *ui;
    HardwareManager* m_hwManager;
    AudioManager* m_audioManager;
    DatabaseManager* m_dbManager;
    QVector<ChannelStrip*> m_channelStrips;
    QTimer* m_connCheckTimer;
    QTimer* m_appPollTimer;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayIconMenu;

    // Caching to prevent UI freezes
    QVector<int> m_lastSliderValues;
    QVector<int> m_lastComValues;
    int m_cachedMinRange;
    int m_cachedMaxRange;
    QTimer* m_audioSyncTimer;

    // Accent color system
    QString m_currentAccent;
    QVector<AccentPreset> m_accentPresets;

    void updateDynamicChannels(int count);
    void loadSettings();
    void setupTrayIcon();
    void buildSettingsPage();
    void buildCalibrationPage();
    void syncAudioEngine();
    void applyAccentColor(const QString& accentHex);
    void initAccentPresets();
};
#endif // MAINWINDOW_H
