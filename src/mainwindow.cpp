#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QTimer>
#include <QApplication>
#include <QStyle>
#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QComboBox>
#include <QFile>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_hwManager(new HardwareManager(this))
    , m_audioManager(new AudioManager(this))
    , m_dbManager(new DatabaseManager(this))
    , m_appPollTimer(new QTimer(this))
    , m_connCheckTimer(new QTimer(this))
    , m_audioSyncTimer(new QTimer(this))
{
    ui->setupUi(this);
    setWindowTitle("Deej Mix");
    setMinimumSize(900, 600);

    statusBar()->showMessage("Scanning for hardware...");

    m_dbManager->initializeDatabase();
    m_audioManager->initialize();

    m_cachedMinRange = m_dbManager->getSetting("MinRange", 0).toInt();
    m_cachedMaxRange = m_dbManager->getSetting("MaxRange", 1023).toInt();

    // Initialize accent color presets and apply saved color
    initAccentPresets();
    QString savedAccent = m_dbManager->getSetting("AccentColor", "#3B82F6").toString();
    applyAccentColor(savedAccent);

    // Setup Sidebar
    ui->sidebarList->addItem("⌂   Home");
    ui->sidebarList->addItem("⌖   Calibration");
    ui->sidebarList->addItem("⛭   Settings");
    connect(ui->sidebarList, &QListWidget::currentRowChanged, ui->stackedWidget, &QStackedWidget::setCurrentIndex);
    ui->sidebarList->setCurrentRow(0);

    connect(m_hwManager, &HardwareManager::hardwareConnected, this, &MainWindow::onHardwareConnected);
    connect(m_hwManager, &HardwareManager::hardwareDisconnected, this, &MainWindow::onHardwareDisconnected);
    connect(m_hwManager, &HardwareManager::sliderValuesReceived, this, &MainWindow::onSliderValuesReceived);

    loadSettings();
    updateDynamicChannels(0); // Show waiting state immediately
    m_hwManager->startAutoDetect();

    buildSettingsPage();
    buildCalibrationPage();
    setupTrayIcon();

    // Poll for new audio apps every 2 seconds
    connect(m_appPollTimer, &QTimer::timeout, this, &MainWindow::refreshAudioSessions);
    m_appPollTimer->start(2000);

    // Start connection health check (1 sec interval)
    connect(m_connCheckTimer, &QTimer::timeout, this, &MainWindow::checkHardwareConnection);
    m_connCheckTimer->start(1000);
    connect(m_audioSyncTimer, &QTimer::timeout, this, &MainWindow::syncAudioEngine);
    m_audioSyncTimer->start(16);
}

MainWindow::~MainWindow()
{
    m_appPollTimer->stop();
    m_connCheckTimer->stop();
    m_audioSyncTimer->stop();
    // Stop hardware while the UI and channel state are still alive. QObject
    // deletes child managers only after MainWindow's members are destroyed.
    disconnect(m_hwManager, nullptr, this, nullptr);
    m_hwManager->disconnectDevice();
    delete ui;
}

void MainWindow::initAccentPresets()
{
    m_accentPresets = {
        {"Blue",   "#3B82F6", "#1E3A8A"},
        {"Cyan",   "#06B6D4", "#0E4F5C"},
        {"Green",  "#10B981", "#065F46"},
        {"Purple", "#8B5CF6", "#4C1D95"},
        {"Orange", "#F59E0B", "#92400E"},
        {"Red",    "#EF4444", "#991B1B"},
        {"Pink",   "#EC4899", "#9D174D"},
    };
}

void MainWindow::applyAccentColor(const QString& accentHex)
{
    m_currentAccent = accentHex;

    // Find matching dark variant from presets
    QString darkHex = "#1E3A8A"; // default dark blue
    for (const auto& preset : m_accentPresets) {
        if (preset.accent.compare(accentHex, Qt::CaseInsensitive) == 0) {
            darkHex = preset.accentDark;
            break;
        }
    }

    // Load QSS template and replace accent placeholders
    QFile file(":/styles.qss");
    if (file.open(QFile::ReadOnly)) {
        QString qss = QString::fromUtf8(file.readAll());
        qss.replace("%ACCENT_DARK%", darkHex);
        qss.replace("%ACCENT%", accentHex);
        qApp->setStyleSheet(qss);
    }

    // Update all knob widgets with the new accent color
    QColor accentColor(accentHex);
    for (auto* strip : m_channelStrips) {
        strip->setAccentColor(accentColor);
    }
}

void MainWindow::loadSettings()
{
    // Load visual preference (false = slider, true = knob)
    bool useKnobs = m_dbManager->getSetting("UseKnobs", false).toBool();
    for(auto* strip : m_channelStrips) {
        strip->setVisualMode(useKnobs);
    }
}

void MainWindow::onHardwareConnected(const QString& portName)
{
    qDebug() << "Hardware connected on" << portName;
    statusBar()->showMessage("Hardware Connected: " + portName);
}

void MainWindow::checkHardwareConnection()
{
    if (!m_hwManager->isConnected()) {
        statusBar()->showMessage("Hardware not detected – awaiting connection", 2000);
    }
}
void MainWindow::onHardwareDisconnected()
{
    qDebug() << "Hardware disconnected";
    statusBar()->showMessage("Hardware Disconnected. Scanning...");
    updateDynamicChannels(0); // Clear UI
    m_lastSliderValues.clear();
    m_lastComValues.clear();
}

void MainWindow::onSliderValuesReceived(const QVector<int>& values)
{
    if (!m_hwManager->isConnected()) {
        // No hardware, ignore values
        return;
    }
    if (m_lastSliderValues.isEmpty() || m_lastSliderValues.size() < values.size()) {
        updateDynamicChannels(values.size());
        m_lastSliderValues.resize(values.size());
        m_lastSliderValues.fill(-1);
        m_lastComValues.resize(values.size());
        m_lastComValues.fill(-1);
    } else if (values.size() < m_lastSliderValues.size()) {
        // Ignore incomplete frames from bad serial code or chunked buffers!
        return;
    }

    int minRange = m_cachedMinRange;
    int maxRange = m_cachedMaxRange;
    if (maxRange <= minRange) maxRange = minRange + 1;

    for (int i = 0; i < values.size(); ++i) {
        int rawVal = values[i];
        int val = rawVal;

        if (rawVal <= minRange) {
            val = 0;
        } else if (rawVal >= maxRange) {
            val = 1023;
        } else {
            val = ((rawVal - minRange) * 1023) / (maxRange - minRange);
        }

        // Skip updating if value hasn't changed enough to prevent COM thrashing and UI starvation
        // Add a threshold of 3 to filter out analog hardware jitter!
        if (m_lastSliderValues[i] != -1 && std::abs(m_lastSliderValues[i] - val) < 3) {
            continue;
        }

        m_lastSliderValues[i] = val;

        // Instant UI Update ONLY - DO NOT CALL COM SYNC HERE!
        m_channelStrips[i]->updateVolume(val);
    }
}

void MainWindow::syncAudioEngine()
{
    // Runs on a separate Qt Event Loop timer at 60 FPS
    // Decouples the laggy COM interface from the high-speed UI/Serial parsing loop!
    for (int i = 0; i < m_lastSliderValues.size(); ++i) {
        if (m_lastSliderValues[i] == m_lastComValues[i] || m_lastSliderValues[i] == -1) {
            continue; // No change since last sync
        }

        m_lastComValues[i] = m_lastSliderValues[i];

        if (i < m_channelStrips.size()) {
            QString appName = m_channelStrips[i]->getAssignedApp();
            if (!appName.isEmpty()) {
                float normalizedVolume = m_lastComValues[i] / 1023.0f;
                if (appName == "[Master Volume]") {
                    m_audioManager->setMasterVolume(normalizedVolume, true);
                } else if (appName == "[System Microphone]") {
                    m_audioManager->setMicrophoneVolume(normalizedVolume, true);
                } else {
                    m_audioManager->setAppVolume(appName, normalizedVolume, true);
                }
            }
        }
    }
}

void MainWindow::onAppAssignmentChanged(int channelId, const QString& appName)
{
    // Save to database when user changes dropdown
    qDebug() << "Channel" << channelId << "assigned to" << appName;
    m_dbManager->setChannelApp(channelId, appName);
    if (channelId >= 0 && channelId < m_lastComValues.size()) {
        m_lastComValues[channelId] = -1;
    }
}

void MainWindow::refreshAudioSessions()
{
    // Get the latest active audio sessions
    QStringList activeApps = m_audioManager->getActiveAudioSessions();

    // Update the dropdowns in each channel strip
    for(auto* strip : m_channelStrips) {
        strip->populateApps(activeApps);
    }
    // A stream may have been recreated even if its application name is the
    // same. Reapply physical levels on each poll so it resumes without a turn
    // of the knob. This also retries channels whose apps were previously absent.
    m_lastComValues.fill(-1);
}

void MainWindow::updateDynamicChannels(int count)
{
    QLayoutItem *child;
    while ((child = ui->homeLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    m_channelStrips.clear();

    if (count == 0) {
        QLabel* waitingLabel = new QLabel("Waiting for Deej Hardware connection...\nPlease plug in your Arduino.", this);
        waitingLabel->setAlignment(Qt::AlignCenter);
        QFont f = waitingLabel->font();
        f.setPointSize(16);
        waitingLabel->setFont(f);
        waitingLabel->setStyleSheet("color: #64748B;");
        ui->homeLayout->addWidget(waitingLabel);
        ui->homeLayout->addStretch();
        return;
    }

    QStringList activeApps = m_audioManager->getActiveAudioSessions();
    bool useKnobs = m_dbManager->getSetting("UseKnobs", false).toBool();
    
    // Choose theme color based on mode
    QColor themeColor = useKnobs ? QColor("#A855F7") /* Purple for Knobs */ : QColor("#0EA5E9") /* Blue for Sliders */;

    // Add a stretch at the beginning to center the cards horizontally
    ui->homeLayout->addStretch();

    for (int i = 0; i < count; ++i) {
        ChannelStrip* strip = new ChannelStrip(i, this);
        strip->setVisualMode(useKnobs);
        strip->setAccentColor(themeColor);
        
        strip->populateApps(activeApps);

        QString savedApp = m_dbManager->getChannelApp(i);
        if (!savedApp.isEmpty()) {
            strip->setAssignedApp(savedApp);
        }

        connect(strip, &ChannelStrip::appAssignmentChanged, this, &MainWindow::onAppAssignmentChanged);

        ui->homeLayout->addWidget(strip);
        m_channelStrips.append(strip);
    }

    // Add a stretch at the end
    ui->homeLayout->addStretch();
}

void MainWindow::setupTrayIcon()
{
    m_trayIconMenu = new QMenu(this);

    QAction* restoreAction = new QAction("Show DeejMix", this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    QAction* refreshAction = new QAction("Refresh", this);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshDevices);

    QAction* quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, &MainWindow::trayQuit);

    m_trayIconMenu->addAction(restoreAction);
    m_trayIconMenu->addAction(refreshAction);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(quitAction);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayIconMenu);

    QIcon icon = style()->standardIcon(QStyle::SP_MediaVolume);
    if (icon.isNull()) {
        icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("DeejMix Background Service");

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);

    m_trayIcon->show();
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::trayQuit()
{
    QApplication::quit();
}

void MainWindow::refreshDevices()
{
    m_appPollTimer->stop();
    m_audioSyncTimer->stop();
    m_hwManager->disconnectDevice();

    const bool audioReady = m_audioManager->refresh();
    refreshAudioSessions();
    m_hwManager->startAutoDetect();
    m_appPollTimer->start();
    m_audioSyncTimer->start();

    const QString message = audioReady
        ? "Audio refreshed. Scanning for hardware..."
        : "Audio unavailable. Scanning for hardware; try Refresh again when audio is available.";
    statusBar()->showMessage(message, 5000);
    m_trayIcon->showMessage("DeejMix Refresh", message,
        audioReady ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon->isVisible()) {
        hide();
        m_trayIcon->showMessage("DeejMix", "Minimized to system tray. Right-click to quit.",
                                QSystemTrayIcon::Information, 3000);
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::buildSettingsPage()
{
    QVBoxLayout* settingsLayout = new QVBoxLayout(ui->pageSettings);
    settingsLayout->setContentsMargins(40, 40, 40, 40);
    settingsLayout->setSpacing(20);

    QLabel* title = new QLabel("Settings", this);
    QFont f = title->font();
    f.setPointSize(24);
    f.setBold(true);
    title->setFont(f);
    settingsLayout->addWidget(title);

    // ── Visual Preferences Group ──
    QGroupBox* visualGroup = new QGroupBox("Visual Preferences", this);
    QVBoxLayout* visualLayout = new QVBoxLayout(visualGroup);
    visualLayout->setSpacing(14);

    QCheckBox* useKnobsCheck = new QCheckBox("Use Knobs instead of Sliders", this);
    useKnobsCheck->setChecked(m_dbManager->getSetting("UseKnobs", false).toBool());

    connect(useKnobsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_dbManager->setSetting("UseKnobs", checked);
        for(auto* strip : m_channelStrips) {
            strip->setVisualMode(checked);
        }
    });

    visualLayout->addWidget(useKnobsCheck);
    settingsLayout->addWidget(visualGroup);

    // ── Accent Color Group ──
    QGroupBox* colorGroup = new QGroupBox("Accent Color", this);
    QVBoxLayout* colorLayout = new QVBoxLayout(colorGroup);
    colorLayout->setSpacing(12);

    QLabel* colorInfo = new QLabel("Choose a theme accent color for the entire application.", this);
    colorInfo->setObjectName("SecondaryText");
    colorInfo->setWordWrap(true);
    colorLayout->addWidget(colorInfo);

    QComboBox* colorCombo = new QComboBox(this);
    for (const auto& preset : m_accentPresets) {
        colorCombo->addItem(preset.name, preset.accent);
    }

    // Select the currently active accent
    for (int i = 0; i < m_accentPresets.size(); ++i) {
        if (m_accentPresets[i].accent.compare(m_currentAccent, Qt::CaseInsensitive) == 0) {
            colorCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, colorCombo](int index) {
        if (index < 0 || index >= m_accentPresets.size()) return;
        QString hex = m_accentPresets[index].accent;
        m_dbManager->setSetting("AccentColor", hex);
        applyAccentColor(hex);
    });

    colorLayout->addWidget(colorCombo);
    settingsLayout->addWidget(colorGroup);

    settingsLayout->addStretch();
}

void MainWindow::buildCalibrationPage()
{
    QVBoxLayout* calibLayout = new QVBoxLayout(ui->pageCalibration);
    calibLayout->setContentsMargins(40, 40, 40, 40);
    calibLayout->setSpacing(20);

    QLabel* title = new QLabel("Calibration", this);
    QFont f = title->font();
    f.setPointSize(24);
    f.setBold(true);
    title->setFont(f);
    calibLayout->addWidget(title);

    QGroupBox* boundsGroup = new QGroupBox("Hardware Value Bounds (0-1023)", this);
    QVBoxLayout* boundsLayout = new QVBoxLayout(boundsGroup);

    QLabel* infoLabel = new QLabel("Adjust these values if your sliders never reach exactly 0% or 100%.", this);
    infoLabel->setWordWrap(true);
    boundsLayout->addWidget(infoLabel);

    QHBoxLayout* minLayout = new QHBoxLayout();
    minLayout->addWidget(new QLabel("Minimum Range (Deadzone):", this));
    QSpinBox* minSpinBox = new QSpinBox(this);
    minSpinBox->setRange(0, 500);
    minSpinBox->setValue(m_dbManager->getSetting("MinRange", 0).toInt());
    minLayout->addWidget(minSpinBox);
    boundsLayout->addLayout(minLayout);

    QHBoxLayout* maxLayout = new QHBoxLayout();
    maxLayout->addWidget(new QLabel("Maximum Range:", this));
    QSpinBox* maxSpinBox = new QSpinBox(this);
    maxSpinBox->setRange(500, 1023);
    maxSpinBox->setValue(m_dbManager->getSetting("MaxRange", 1023).toInt());
    maxLayout->addWidget(maxSpinBox);
    boundsLayout->addLayout(maxLayout);

    connect(minSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_dbManager->setSetting("MinRange", value);
        m_cachedMinRange = value;
    });

    connect(maxSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_dbManager->setSetting("MaxRange", value);
        m_cachedMaxRange = value;
    });

    calibLayout->addWidget(boundsGroup);
    calibLayout->addStretch();
}
