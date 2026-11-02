#include "channelstrip.h"
#include <QHBoxLayout>
#include <QApplication>
#include <QStyle>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QSignalBlocker>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif
#include <QGraphicsDropShadowEffect>

ChannelStrip::ChannelStrip(int channelId, QWidget *parent)
    : QWidget(parent), m_channelId(channelId)
{
    setObjectName("ChannelCard"); // Important for QSS
    setAttribute(Qt::WA_StyledBackground, true); // REQUIRED for QWidget to paint QSS backgrounds!
    
    // Lock proportions to match the premium card design
    setFixedSize(250, 380);

    // Add the premium drop shadow for floating glassmorphism effect
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 150));
    shadow->setOffset(0, 6);
    this->setGraphicsEffect(shadow);

    setupUi();
}

ChannelStrip::~ChannelStrip()
{
}

void ChannelStrip::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ── Header row: icon + channel name + options ──
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(28, 28);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setObjectName("AppIcon");
    QIcon defaultIcon = qApp->style()->standardIcon(QStyle::SP_MediaVolume);
    m_iconLabel->setPixmap(defaultIcon.pixmap(24, 24));

    m_channelNameLabel = new QLabel(QString("Channel %1").arg(m_channelId + 1), this);
    m_channelNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_channelNameLabel->setObjectName("Header");
    QFont font = m_channelNameLabel->font();
    font.setPointSize(11);
    font.setBold(true);
    m_channelNameLabel->setFont(font);

    QLabel* optionsLabel = new QLabel("⋮", this);
    optionsLabel->setStyleSheet("color: #64748B; font-weight: bold; font-size: 16px;");

    headerLayout->addWidget(m_iconLabel);
    headerLayout->addWidget(m_channelNameLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(optionsLabel);

    m_appDropdown = new QComboBox(this);
    m_appDropdown->addItem("Select App...");
    connect(m_appDropdown, &QComboBox::currentTextChanged, this, [this]{
        emit appAssignmentChanged(m_channelId, getAssignedApp());
        updateAppIcon();
    });

    m_slider = new QSlider(Qt::Vertical, this);
    m_slider->setRange(0, 100);
    m_slider->setValue(0);
    m_slider->setTickPosition(QSlider::TicksRight); // Ticks only on right side
    m_slider->setTickInterval(10);
    m_slider->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setMinimumHeight(150);

    m_knob = new KnobWidget(this);
    m_knob->setValue(0);
    m_knob->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_knob->setFocusPolicy(Qt::NoFocus);

    // Stack to switch between slider and knob
    m_visualStack = new QStackedWidget(this);
    m_visualStack->addWidget(m_slider);
    m_visualStack->addWidget(m_knob);

    // Center the stack
    QHBoxLayout* visualLayout = new QHBoxLayout();
    visualLayout->addStretch();
    visualLayout->addWidget(m_visualStack);
    visualLayout->addStretch();

    m_volumeLabel = new QLabel("0%", this);
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    font.setPointSize(16);
    m_volumeLabel->setFont(font);
    
    // Bottom Icon (Speaker or Live)
    m_speakerIcon = new QLabel(this);
    m_speakerIcon->setAlignment(Qt::AlignCenter);
    m_speakerIcon->setObjectName("SpeakerIcon");
    
    QHBoxLayout* speakerLayout = new QHBoxLayout();
    speakerLayout->addStretch();
    speakerLayout->addWidget(m_speakerIcon);
    speakerLayout->addStretch();

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(m_appDropdown);
    mainLayout->addSpacing(10);
    mainLayout->addLayout(visualLayout);
    mainLayout->addSpacing(10);
    mainLayout->addWidget(m_volumeLabel);
    mainLayout->addLayout(speakerLayout);
}

void ChannelStrip::setVisualMode(bool isKnob)
{
    m_isKnobMode = isKnob;
    m_visualStack->setCurrentIndex(isKnob ? 1 : 0);
}

void ChannelStrip::setAccentColor(const QColor& color)
{
    m_knob->setAccentColor(color);
    
    QString darkGlow = color.darker(300).name();
    QString medGlow = color.darker(150).name();
    QString borderStyle = m_isKnobMode ? 
        QString("border: 1px solid %1; border-top: 2px solid %2;").arg(darkGlow, color.name()) :
        QString("border: 1px solid %1; border-left: 2px solid %2;").arg(darkGlow, color.name());
    
    this->setStyleSheet(QString(
        "QWidget#ChannelCard {"
        "    background-color: #0A0E17;"
        "    border-radius: 12px;"
        "    %4"
        "}"
        "QLabel#Header { color: #FFFFFF; }"
        "QSlider::groove:vertical {"
        "    background: #000000;"
        "    width: 6px;"
        "    border-radius: 3px;"
        "}"
        "QSlider::handle:vertical {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #E0E4EC, stop:0.5 #808898, stop:1 #E0E4EC);"
        "    height: 12px;"
        "    margin: 0 -14px;"
        "    border-radius: 4px;"
        "    border-top: 1px solid #FFFFFF;"
        "    border-bottom: 2px solid %2;"
        "}"
        "QSlider::add-page:vertical {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %2, stop:0.5 %3, stop:1 %2);"
        "    border-radius: 3px;"
        "}"
        "QSlider::sub-page:vertical {"
        "    background: transparent;"
        "}"
    ).arg(darkGlow, medGlow, color.name(), borderStyle));

    m_volumeLabel->setStyleSheet(QString("color: %1;").arg(color.name()));

    if (m_isKnobMode) {
        m_speakerIcon->setText("  ||| Live  ");
        QFont f; f.setPointSize(10); f.setBold(true);
        m_speakerIcon->setFont(f);
        m_speakerIcon->setStyleSheet(QString("background-color: transparent; color: %1; border: 1px solid %1; border-radius: 4px; padding: 2px;").arg(color.name()));
    } else {
        m_speakerIcon->setText("🔊");
        QFont f; f.setPointSize(14);
        m_speakerIcon->setFont(f);
        m_speakerIcon->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 8px; padding: 4px 12px;").arg(darkGlow, color.name()));
    }
}

void ChannelStrip::updateVolume(int volume)
{
    // Convert 0-1023 hardware value to 0-100 percentage
    int percentage = (volume * 100) / 1023;
    m_slider->setValue(percentage);
    m_knob->setValue(percentage);
    m_volumeLabel->setText(QString("%1%").arg(percentage));
}

void ChannelStrip::populateApps(const QStringList& apps)
{
    const QString current = getAssignedApp();

    const QSignalBlocker blocker(m_appDropdown);
    m_appDropdown->clear();
    m_appDropdown->addItem("Select App...");
    m_appDropdown->addItem("[Master Volume]");
    m_appDropdown->addItem("[System Microphone]");
    m_appDropdown->addItems(apps);

    if (!current.isEmpty()) {
        int idx = m_appDropdown->findText(current);
        if (idx == -1) {
            // A channel assignment outlives an application's playback stream.
            m_appDropdown->addItem(current);
            idx = m_appDropdown->count() - 1;
            m_appDropdown->setItemData(idx,
                "Not currently playing. This assignment is kept until you change it.",
                Qt::ToolTipRole);
        }
        m_appDropdown->setCurrentIndex(idx);
    }
    updateAppIcon();
}

QString ChannelStrip::getAssignedApp() const
{
    QString text = m_appDropdown->currentText();
    return (text == "Select App...") ? "" : text;
}

void ChannelStrip::setAssignedApp(const QString& appName)
{
    const QSignalBlocker blocker(m_appDropdown);
    if (appName.isEmpty()) {
        m_appDropdown->setCurrentIndex(0);
        updateAppIcon();
        return;
    }
    int idx = m_appDropdown->findText(appName);
    if (idx != -1) {
        m_appDropdown->setCurrentIndex(idx);
    } else {
        m_appDropdown->addItem(appName);
        m_appDropdown->setCurrentText(appName);
    }

    updateAppIcon();
}

void ChannelStrip::updateAppIcon()
{
    QString appName = m_appDropdown->currentText();
    QIcon icon = fetchAppIcon(appName);
    if (!icon.isNull()) {
        m_iconLabel->setPixmap(icon.pixmap(20, 20));
    } else {
        // Fallback to default audio icon
        QIcon fallback = qApp->style()->standardIcon(QStyle::SP_MediaVolume);
        m_iconLabel->setPixmap(fallback.pixmap(20, 20));
    }
}

QIcon ChannelStrip::fetchAppIcon(const QString& appName)
{
    if (appName.isEmpty() || appName == "Select App...")
        return QIcon();

    // Special entries use built-in Qt icons
    if (appName == "[Master Volume]")
        return qApp->style()->standardIcon(QStyle::SP_MediaVolume);
    if (appName == "[System Microphone]")
        return qApp->style()->standardIcon(QStyle::SP_DriveCDIcon);

#ifdef Q_OS_WIN
    // Find the full executable path by scanning running processes
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return QIcon();

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    QString target = appName.toLower();
    DWORD targetPid = 0;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (QString::fromWCharArray(pe.szExeFile).toLower() == target) {
                targetPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);

    if (targetPid == 0) return QIcon();

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, targetPid);
    if (!hProcess) return QIcon();

    WCHAR path[MAX_PATH];
    QString fullPath;
    if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH)) {
        fullPath = QString::fromWCharArray(path);
    }
    CloseHandle(hProcess);

    if (fullPath.isEmpty()) return QIcon();

    // Use Qt's built-in file icon provider to get the exe icon
    QFileIconProvider provider;
    return provider.icon(QFileInfo(fullPath));
#else
    return QIcon();
#endif
}
