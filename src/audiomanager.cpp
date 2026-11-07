#include "audiomanager.h"
#include <QDebug>

#ifdef Q_OS_LINUX
#include <pulse/pulseaudio.h>
#include <QHash>
#include <QVector>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <math.h>
#include <psapi.h>

static QString getProcessName(DWORD pid) {
    QString name;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        WCHAR buffer[MAX_PATH];
        if (GetModuleFileNameExW(hProcess, NULL, buffer, MAX_PATH)) {
            name = QString::fromWCharArray(buffer).section('\\', -1).toLower();
        }
        CloseHandle(hProcess);
    }
    return name;
}

// Helper to convert linear 0.0-1.0 to a logarithmic scale
float linearToLogarithmic(float linearValue) {
    // A simple exponential curve matching human hearing
    // volume = (e^(alpha * x) - 1) / (e^alpha - 1)
    if (linearValue <= 0.0f) return 0.0f;
    if (linearValue >= 1.0f) return 1.0f;
    
    const float alpha = 6.908f; // ln(1000) for a 60dB dynamic range
    return (exp(alpha * linearValue) - 1.0f) / (exp(alpha) - 1.0f);
}

#endif

#ifdef Q_OS_LINUX
// ─────────────────────────────────────────────────────────────────────────────
// Linux audio backend: PulseAudio / PipeWire (via its pulse-compat server).
//
// On Linux there is no per-PID "audio session" concept like Windows WASAPI.
// Instead, each running application appears as a PulseAudio *sink input* whose
// `application.name` property identifies it. We enumerate the default sink's
// sink inputs and set volume per input. This single code path works against
// both classic PulseAudio and PipeWire (which speaks the PulseAudio protocol).
//
// We use a small synchronous helper pattern: spin the PulseAudio mainloop until
// a request's reply arrives. This keeps the AudioManager API identical to the
// Windows build (simple blocking calls), which is what MainWindow expects.
// ─────────────────────────────────────────────────────────────────────────────

// Synchronously run the PulseAudio main loop until the given operation
// finishes (or a generous iteration budget is reached, as a safety net).
// Uses BLOCKING iteration so each call returns as soon as the server's
// reply arrives, which is far more reliable than a fixed count of
// non-blocking iterations (that can exhaust its budget before the reply
// is even readable from the socket).
static bool pa_spin_op(AudioManager *am, pa_operation *op, int maxIters = 2000) {
    if (!am || !op || !am->m_pulseMainloop) return false;
    int ret = 0;
    for (int i = 0; i < maxIters; ++i) {
        if (pa_operation_get_state(op) != PA_OPERATION_RUNNING) break;
        pa_mainloop_iterate(am->m_pulseMainloop, 1, &ret);
        if (ret != 0) break;
    }
    const bool done = (pa_operation_get_state(op) == PA_OPERATION_DONE);
    pa_operation_unref(op);
    return done;
}

// Wait until the context becomes READY (or FAILED).
static bool pa_spin_until_ready(AudioManager *am) {
    if (!am || !am->m_pulseContext || !am->m_pulseMainloop) return false;
    for (int i = 0; i < 20000; ++i) {
        pa_context_state_t st = pa_context_get_state(am->m_pulseContext);
        if (st == PA_CONTEXT_READY) return true;
        if (st == PA_CONTEXT_FAILED) return false;
        int ret = 0;
        pa_mainloop_iterate(am->m_pulseMainloop, 1, &ret);
        if (ret != 0) break;
    }
    return false;
}

// Query the server for the default sink/source names (there is no dedicated
// getter in this PulseAudio build, so we read pa_server_info).
struct PaDefaultDevices {
    QString defaultSink;
    QString defaultSource;
};

static void pa_server_info_cb(pa_context *c, const pa_server_info *si, void *userdata) {
    Q_UNUSED(c);
    auto *res = static_cast<PaDefaultDevices *>(userdata);
    if (si->default_sink_name) res->defaultSink = QString::fromUtf8(si->default_sink_name);
    if (si->default_source_name) res->defaultSource = QString::fromUtf8(si->default_source_name);
}

static bool pa_get_default_devices(AudioManager *am, PaDefaultDevices *res) {
    pa_operation *op = pa_context_get_server_info(am->m_pulseContext, pa_server_info_cb, res);
    if (op && !pa_spin_op(am, op)) return false;
    return !res->defaultSink.isEmpty() || !res->defaultSource.isEmpty();
}

// Build a stable, human-readable identifier for a sink input, matching the
// convention used by getActiveAudioSessions().
static QString pa_sink_input_name(const pa_sink_input_info *si) {
    if (si->proplist) {
        const char *appName = pa_proplist_gets(si->proplist, "application.name");
        if (appName && *appName) return QString::fromUtf8(appName);
        const char *mediaName = pa_proplist_gets(si->proplist, "media.name");
        const char *mediaRole = pa_proplist_gets(si->proplist, "media.role");
        if (mediaName && mediaRole)
            return QString::fromUtf8(mediaRole) + " - " + QString::fromUtf8(mediaName);
        if (mediaName) return QString::fromUtf8(mediaName);
    }
    if (si->name && *si->name) return QString::fromUtf8(si->name);
    return QString();
}

struct PaInputEntry {
    uint32_t index;
    QString name;
};

static void pa_sink_inputs_cb(pa_context *c, const pa_sink_input_info *sii, int eol, void *userdata) {
    Q_UNUSED(c);
    if (eol) return;
    auto *out = static_cast<QVector<PaInputEntry> *>(userdata);
    out->append(PaInputEntry{ sii->index, pa_sink_input_name(sii) });
}

// Snapshot every sink input on the server (index + resolved app name).
static QVector<PaInputEntry> pa_get_sink_inputs(AudioManager *am) {
    QVector<PaInputEntry> out;
    pa_operation *op = pa_context_get_sink_input_info_list(am->m_pulseContext, pa_sink_inputs_cb, &out);
    if (op && !pa_spin_op(am, op)) out.clear();
    return out;
}

// Collect the deduplicated list of application names from every sink input.
static QStringList pa_active_app_names(AudioManager *am) {
    QStringList apps;
    for (const PaInputEntry &e : pa_get_sink_inputs(am)) {
        if (!e.name.isEmpty() && !apps.contains(e.name)) apps.append(e.name);
    }
    return apps;
}

static void pa_set_input_volume(AudioManager *am, uint32_t inputIdx, float volume) {
    pa_cvolume cvol;
    pa_cvolume_init(&cvol);
    pa_cvolume_set(&cvol, 2, (uint32_t)(qBound(0.0f, volume, 1.0f) * PA_VOLUME_NORM));
    pa_operation *op = pa_context_set_sink_input_volume(am->m_pulseContext, inputIdx, &cvol, nullptr, nullptr);
    pa_spin_op(am, op);
}

#endif

AudioManager::AudioManager(QObject *parent) : QObject(parent), m_initialized(false)
{
}

AudioManager::~AudioManager()
{
    shutdown();
}

void AudioManager::shutdown()
{
#ifdef Q_OS_WIN
    if (m_initialized) {
        CoUninitialize();
    }
#endif
#ifdef Q_OS_LINUX
    if (m_pulseContext) {
        pa_context_disconnect(m_pulseContext);
        pa_context_unref(m_pulseContext);
        m_pulseContext = nullptr;
    }
    if (m_pulseMainloop) {
        pa_mainloop_free(m_pulseMainloop);
        m_pulseMainloop = nullptr;
    }
#endif
    m_initialized = false;
}

bool AudioManager::refresh()
{
#ifdef Q_OS_LINUX
    // Reconnect even if initialize() previously succeeded: the audio server
    // may have restarted since then, leaving the old context unusable.
    shutdown();
#endif
    // Windows reacquires endpoints and sessions on each query; retain its
    // thread's COM initialization rather than repeatedly initializing it.
    return m_initialized || initialize();
}

bool AudioManager::initialize()
{
#ifdef Q_OS_WIN
    // Qt initializes COM as STA (COINIT_APARTMENTTHREADED). Using MULTITHREADED causes RPC_E_CHANGED_MODE.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != (HRESULT)0x80010106L) {
        qDebug() << "Failed to initialize COM library" << hr;
        return false;
    }
    m_initialized = true;
    return true;
#elif defined(Q_OS_LINUX)
    if (m_initialized) return true;

    m_pulseMainloop = pa_mainloop_new();
    if (!m_pulseMainloop) {
        qWarning() << "Failed to create PulseAudio mainloop";
        return false;
    }

    m_pulseContext = pa_context_new(pa_mainloop_get_api(m_pulseMainloop), "DeejMix");
    if (!m_pulseContext) {
        qWarning() << "Failed to create PulseAudio context";
        pa_mainloop_free(m_pulseMainloop);
        m_pulseMainloop = nullptr;
        return false;
    }

    pa_context_connect(m_pulseContext, nullptr, PA_CONTEXT_NOFLAGS, 0);

    if (!pa_spin_until_ready(this)) {
        qWarning() << "PulseAudio context failed to reach READY state";
        pa_context_unref(m_pulseContext);
        m_pulseContext = nullptr;
        pa_mainloop_free(m_pulseMainloop);
        m_pulseMainloop = nullptr;
        return false;
    }

    m_initialized = true;
    qDebug() << "[AudioManager] Linux PulseAudio initialized (client=DeejMix)";
    return true;
#else
    return false;
#endif
}

void AudioManager::setAppVolume(const QString& appName, float volume, bool useLogarithmic)
{
    if (!m_initialized) return;

#ifdef Q_OS_WIN
    float finalVolume = useLogarithmic ? linearToLogarithmic(volume) : volume;
    
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
    if (FAILED(hr) || !deviceEnumerator) return;

    IMMDevice *defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    deviceEnumerator->Release();
    if (FAILED(hr) || !defaultDevice) return;

    IAudioSessionManager2 *sessionManager = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&sessionManager);
    defaultDevice->Release();
    if (FAILED(hr) || !sessionManager) return;

    IAudioSessionEnumerator *sessionEnumerator = NULL;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    sessionManager->Release();
    if (FAILED(hr) || !sessionEnumerator) return;

    int count;
    hr = sessionEnumerator->GetCount(&count);
    if (SUCCEEDED(hr)) {
        QString targetName = appName.toLower();
        for (int i = 0; i < count; i++) {
            IAudioSessionControl *sessionControl = NULL;
            if (SUCCEEDED(sessionEnumerator->GetSession(i, &sessionControl)) && sessionControl) {
                IAudioSessionControl2 *sessionControl2 = NULL;
                if (SUCCEEDED(sessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&sessionControl2)) && sessionControl2) {
                    DWORD pid = 0;
                    sessionControl2->GetProcessId(&pid);
                    if (pid > 0 && getProcessName(pid) == targetName) {
                        ISimpleAudioVolume *simpleAudioVolume = NULL;
                        if (SUCCEEDED(sessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&simpleAudioVolume)) && simpleAudioVolume) {
                            simpleAudioVolume->SetMasterVolume(finalVolume, NULL);
                            simpleAudioVolume->Release();
                        }
                    }
                    sessionControl2->Release();
                }
                sessionControl->Release();
            }
        }
    }
    sessionEnumerator->Release();
#elif defined(Q_OS_LINUX)
    float finalVolume = qBound(0.0f, volume, 1.0f);
    for (const PaInputEntry &input : pa_get_sink_inputs(this)) {
        if (input.name == appName) {
            pa_set_input_volume(this, input.index, finalVolume);
        }
    }
#endif
}

void AudioManager::setMasterVolume(float volume, bool useLogarithmic)
{
    if (!m_initialized) return;

#ifdef Q_OS_WIN
    float finalVolume = useLogarithmic ? linearToLogarithmic(volume) : volume;
    
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
    if (FAILED(hr) || !deviceEnumerator) return;
    
    IMMDevice *defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr) || !defaultDevice) {
        deviceEnumerator->Release();
        return;
    }
    
    IAudioEndpointVolume *endpointVolume = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpointVolume);
    if (FAILED(hr) || !endpointVolume) {
        defaultDevice->Release();
        deviceEnumerator->Release();
        return;
    }
    
    endpointVolume->SetMasterVolumeLevelScalar(finalVolume, NULL);
    
    endpointVolume->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
#elif defined(Q_OS_LINUX)
    float finalVolume = qBound(0.0f, volume, 1.0f);
    PaDefaultDevices dev;
    if (pa_get_default_devices(this, &dev) && !dev.defaultSink.isEmpty()) {
        pa_cvolume cvol;
        pa_cvolume_init(&cvol);
        pa_cvolume_set(&cvol, 2, (uint32_t)(finalVolume * PA_VOLUME_NORM));
        pa_operation *op = pa_context_set_sink_volume_by_name(m_pulseContext,
            dev.defaultSink.toUtf8().constData(), &cvol, nullptr, nullptr);
        pa_spin_op(this, op);
    }
#endif
}

void AudioManager::setMicrophoneVolume(float volume, bool useLogarithmic)
{
    if (!m_initialized) return;

#ifdef Q_OS_WIN
    float finalVolume = useLogarithmic ? linearToLogarithmic(volume) : volume;
    
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
    if (FAILED(hr) || !deviceEnumerator) return;
    
    IMMDevice *defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice);
    if (FAILED(hr) || !defaultDevice) {
        deviceEnumerator->Release();
        return;
    }
    
    IAudioEndpointVolume *endpointVolume = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpointVolume);
    if (FAILED(hr) || !endpointVolume) {
        defaultDevice->Release();
        deviceEnumerator->Release();
        return;
    }
    
    endpointVolume->SetMasterVolumeLevelScalar(finalVolume, NULL);
    
    endpointVolume->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
#elif defined(Q_OS_LINUX)
    float finalVolume = qBound(0.0f, volume, 1.0f);
    PaDefaultDevices dev;
    if (pa_get_default_devices(this, &dev) && !dev.defaultSource.isEmpty()) {
        pa_cvolume cvol;
        pa_cvolume_init(&cvol);
        pa_cvolume_set(&cvol, 2, (uint32_t)(finalVolume * PA_VOLUME_NORM));
        QByteArray name = dev.defaultSource.toUtf8();
        pa_operation *op = pa_context_set_source_volume_by_name(m_pulseContext,
            name.constData(), &cvol, nullptr, nullptr);
        pa_spin_op(this, op);
    }
#endif
}

QStringList AudioManager::getActiveAudioSessions()
{
    QStringList apps;
    if (!m_initialized) return apps;

#ifdef Q_OS_WIN
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
    if (FAILED(hr) || !deviceEnumerator) return apps;

    IMMDevice *defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    deviceEnumerator->Release();
    if (FAILED(hr) || !defaultDevice) return apps;

    IAudioSessionManager2 *sessionManager = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&sessionManager);
    defaultDevice->Release();
    if (FAILED(hr) || !sessionManager) return apps;

    IAudioSessionEnumerator *sessionEnumerator = NULL;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    sessionManager->Release();
    if (FAILED(hr) || !sessionEnumerator) return apps;

    int count;
    hr = sessionEnumerator->GetCount(&count);
    if (SUCCEEDED(hr)) {
        for (int i = 0; i < count; i++) {
            IAudioSessionControl *sessionControl = NULL;
            if (SUCCEEDED(sessionEnumerator->GetSession(i, &sessionControl)) && sessionControl) {
                IAudioSessionControl2 *sessionControl2 = NULL;
                if (SUCCEEDED(sessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&sessionControl2)) && sessionControl2) {
                    DWORD pid = 0;
                    sessionControl2->GetProcessId(&pid);
                    if (pid > 0) {
                        QString procName = getProcessName(pid);
                        if (!procName.isEmpty() && !apps.contains(procName)) {
                            apps.append(procName);
                        }
                    }
                    sessionControl2->Release();
                }
                sessionControl->Release();
            }
        }
    }
    sessionEnumerator->Release();
    apps.sort(Qt::CaseInsensitive);
#elif defined(Q_OS_LINUX)
    apps = pa_active_app_names(this);
    apps.sort(Qt::CaseInsensitive);
#endif

    return apps;
}
