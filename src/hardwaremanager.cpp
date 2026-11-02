#include "hardwaremanager.h"
#include <QSerialPortInfo>
#include <QDebug>
#include <QSignalBlocker>

HardwareManager::HardwareManager(QObject *parent) 
    : QObject(parent), m_serial(new QSerialPort(this)), m_currentPortIndex(0), m_currentBaudIndex(0), m_isSniffing(false), m_isConnected(false)
{
    m_baudRates = {QSerialPort::Baud9600, QSerialPort::Baud115200};
    m_scanTimer = new QTimer(this);
    m_readTimer = new QTimer(this);
    
    connect(m_serial, &QSerialPort::readyRead, this, &HardwareManager::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &HardwareManager::onPortError);
    connect(m_scanTimer, &QTimer::timeout, this, &HardwareManager::attemptConnection);
    connect(m_readTimer, &QTimer::timeout, this, &HardwareManager::connectionTimeout);
}

HardwareManager::~HardwareManager()
{
    // Never notify a parent window from a child destructor: its UI may already
    // have been destroyed by the time QObject deletes this manager.
    const QSignalBlocker blocker(this);
    disconnectDevice();
}

void HardwareManager::startAutoDetect()
{
    if (m_isConnected) return;
    
    m_isSniffing = true;
    m_currentPortIndex = 0;
    m_currentBaudIndex = 0;
    // Start after the current event has finished, including when restarting
    // from a serial error callback. A subsequent disconnect cancels the probe.
    QTimer::singleShot(0, this, &HardwareManager::attemptConnection);
}

void HardwareManager::disconnectDevice()
{
    const bool wasConnected = m_isConnected;
    m_isConnected = false;
    m_isSniffing = false;
    m_scanTimer->stop();
    m_readTimer->stop();
    const QSignalBlocker serialBlocker(m_serial);
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    if (wasConnected) emit hardwareDisconnected();
}

bool HardwareManager::isConnected() const
{
    return m_isConnected;
}

void HardwareManager::attemptConnection()
{
    if (m_isConnected || !m_isSniffing) return;
    
    if (m_serial->isOpen()) {
        m_serial->close();
    }

    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty() || m_currentPortIndex >= ports.size()) {
        m_currentPortIndex = 0;
        m_currentBaudIndex = 0;
        // Wait 5 seconds before scanning all ports again to avoid freezing
        m_scanTimer->start(5000); 
        return;
    }

    m_scanTimer->stop();
    
    QSerialPortInfo info = ports.at(m_currentPortIndex);
    qDebug() << "Attempting connection to" << info.portName() << "at baud" << m_baudRates[m_currentBaudIndex];
    m_serial->setPort(info);
    m_serial->setBaudRate(m_baudRates[m_currentBaudIndex]);
    
    // Temporarily block signals or do it quickly
    if (m_serial->open(QIODevice::ReadOnly)) {
        m_buffer.clear();
        // Wait up to 3.5 seconds for data. Arduino Uno/Nano reboot on serial open and take ~2s to boot!
        m_readTimer->start(3500); 
    } else {
        m_currentBaudIndex = 0;
        m_currentPortIndex++;
        // Use a QTimer::singleShot to yield back to event loop, preventing freeze!
        QTimer::singleShot(10, this, &HardwareManager::attemptConnection);
    }
}

void HardwareManager::connectionTimeout()
{
    if (!m_isConnected && m_isSniffing) {
        if (m_serial->isOpen()) {
            m_serial->close();
        }
        
        m_currentBaudIndex++;
        if (m_currentBaudIndex >= m_baudRates.size()) {
            m_currentBaudIndex = 0;
            m_currentPortIndex++;
        }
        
        // Yield to event loop
        QTimer::singleShot(10, this, &HardwareManager::attemptConnection);
    }
}

void HardwareManager::onReadyRead()
{
    QByteArray data = m_serial->readAll();
    m_buffer += data;
    qDebug() << "Serial read:" << data;
    
    while (m_buffer.contains('\n')) {
        int newlineIndex = m_buffer.indexOf('\n');
        QString line = m_buffer.left(newlineIndex).trimmed();
        m_buffer.remove(0, newlineIndex + 1);
        
        if (!line.isEmpty()) {
            qDebug() << "Parsed line:" << line;
            processLine(line);
        }
    }
}

void HardwareManager::processLine(const QString& line)
{
    // Deej protocol: "10|50|100|80|20"
    QStringList parts = line.split('|');
    bool valid = true;
    QVector<int> values;
    
    for (const QString& part : parts) {
        bool ok;
        int val = part.toInt(&ok);
        if (!ok || val < 0 || val > 1023) {
            qDebug() << "Invalid part:" << part;
            valid = false;
            break;
        }
        values.append(val);
    }
    
    if (valid && values.size() > 0) {
        if (!m_isConnected) {
            m_isConnected = true;
            m_isSniffing = false;
            m_readTimer->stop();
            qDebug() << "Hardware successfully connected! Values size:" << values.size();
            emit hardwareConnected(m_serial->portName());
        }
        emit sliderValuesReceived(values);
    } else {
        qDebug() << "Line marked invalid. values.size():" << values.size() << " valid:" << valid;
    }
}

void HardwareManager::onPortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::ResourceError && m_isConnected) {
        disconnectDevice();
        // Always resume scanning if we lost connection unexpectedly!
        // Failed probes are handled by attemptConnection/connectionTimeout.
        // startAutoDetect queues the scan rather than re-entering serial I/O.
        startAutoDetect();
    }
}
