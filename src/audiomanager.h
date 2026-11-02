#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>

// Forward declarations for COM interfaces to keep the header clean
struct IAudioSessionManager2;
struct IAudioSessionEnumerator;

#ifdef Q_OS_LINUX
// Forward declarations for PulseAudio to keep the header clean
struct pa_mainloop;
struct pa_context;
#endif

class AudioManager : public QObject
{
    Q_OBJECT
public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager();

    bool initialize();
    
    // Core control
    void setAppVolume(const QString& appName, float volume, bool useLogarithmic = false);
    void setMasterVolume(float volume, bool useLogarithmic = false);
    void setMicrophoneVolume(float volume, bool useLogarithmic = false);

    // Enumeration
    QStringList getActiveAudioSessions();

#ifdef Q_OS_LINUX
    // Exposed so the Linux backend helper functions (defined in audiomanager.cpp,
    // the same translation unit) can drive the PulseAudio context/mainloop.
    pa_mainloop *m_pulseMainloop = nullptr;
    pa_context  *m_pulseContext  = nullptr;
#endif

signals:
    void sessionListChanged(const QStringList& sessions);
    void sessionVolumeChanged(const QString& appName, float volume);

private:
    bool m_initialized;
    
    // Internal COM helper functions will go here
};

#endif // AUDIOMANAGER_H
