#include "audiorouter_win.h"

// windows.h defines min/max macros unless NOMINMAX is set. Those macros would
// interfere with std::min/std::max in ringbuffer.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

#include "adaptive_audio_buffer.h"
#include "ringbuffer.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QString>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <future>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr float kMaxVolume = 5.0f;

// Minimal COM smart pointer that does not depend on ATL and works with both
// MSVC and MinGW.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : m_p(p) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : m_p(o.m_p) { o.m_p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept
    {
        if (this != &o) {
            reset();
            m_p = o.m_p;
            o.m_p = nullptr;
        }
        return *this;
    }
    T* get() const { return m_p; }
    T** put()
    {
        reset();
        return &m_p;
    }
    T* operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
    void reset(T* p = nullptr)
    {
        if (m_p)
            m_p->Release();
        m_p = p;
    }

private:
    T* m_p = nullptr;
};

QString wideToQString(LPCWSTR w)
{
    return w ? QString::fromWCharArray(w) : QString{};
}

std::wstring qToWide(const QString& s)
{
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()));
}

QString hresultText(HRESULT hr)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return QString::fromLatin1(buf);
}

QString winTr(const char* source)
{
    return QCoreApplication::translate("AudioRouterWin", source);
}

// Publish a float through a lock-free 32-bit atomic. A floating-point atomic
// may use a library lock on some Windows toolchains, which would put an
// avoidable lock in the render worker's steady-state path.
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "WASAPI realtime volume requires a lock-free 32-bit atomic");

uint32_t encodeFloat(float value) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float decodeFloat(uint32_t bits) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Register a worker with MMCSS after setup succeeds, immediately before its
// event-driven audio loop. Registration is intentionally best-effort: systems
// without the service still get the same bounded queue and event-driven
// behavior.
class MmcssRegistration final {
public:
    MmcssRegistration() = default;

    void registerCurrentThread() noexcept
    {
        reset();
        m_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &m_taskIndex);
        if (!m_handle)
            m_handle = AvSetMmThreadCharacteristicsW(L"Audio", &m_taskIndex);
    }

    ~MmcssRegistration()
    {
        reset();
    }

    void reset() noexcept
    {
        if (m_handle)
            AvRevertMmThreadCharacteristics(m_handle);
        m_handle = nullptr;
        m_taskIndex = 0;
    }

    MmcssRegistration(const MmcssRegistration&) = delete;
    MmcssRegistration& operator=(const MmcssRegistration&) = delete;

private:
    HANDLE m_handle = nullptr;
    DWORD m_taskIndex = 0;
};

constexpr UINT32 kChannels = 2;
constexpr size_t kQueueBufferDurationMs = 50;
constexpr size_t kQueueTargetDurationMs = 20;
constexpr size_t kQueueSafetyDurationMs = 10;
constexpr size_t kQueueHysteresisDurationMs = 2;

size_t framesForDuration(DWORD sampleRate, size_t durationMs) noexcept
{
    const uint64_t product = static_cast<uint64_t>(sampleRate) * durationMs;
    return static_cast<size_t>(product / 1000 + (product % 1000 != 0 ? 1 : 0));
}

// Check whether a WAVEFORMATEX is 32-bit IEEE float.
bool isFloat32Format(const WAVEFORMATEX* fmt)
{
    if (!fmt)
        return false;
    if (fmt->nChannels == 0
        || fmt->nBlockAlign != fmt->nChannels * sizeof(float)) {
        return false;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && fmt->wBitsPerSample == 32)
        return true;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        const WORD validBits = ext->Samples.wValidBitsPerSample;
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            && fmt->wBitsPerSample == 32
            && (validBits == 0 || validBits == 32);
    }
    return false;
}

bool isSupportedIntegerPcmFormat(const WAVEFORMATEX* fmt)
{
    if (!fmt)
        return false;

    bool isPcm = fmt->wFormatTag == WAVE_FORMAT_PCM;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE
        && fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        isPcm = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    }

    if (!isPcm
        || (fmt->wBitsPerSample != 16
            && fmt->wBitsPerSample != 24
            && fmt->wBitsPerSample != 32)) {
        return false;
    }

    const size_t packedBlockAlign = static_cast<size_t>(fmt->nChannels)
        * (fmt->wBitsPerSample / 8);
    if (fmt->nChannels == 0 || fmt->nBlockAlign != packedBlockAlign)
        return false;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        // The converter below handles packed samples only. Valid PCM bits that
        // are left-aligned in a wider container need a different conversion.
        const WORD validBits = ext->Samples.wValidBitsPerSample;
        if (validBits != 0 && validBits != fmt->wBitsPerSample)
            return false;
    }
    return true;
}

// Convert packed integer PCM to float32 in the range [-1.0, 1.0].
bool convertPcmToFloat32(const BYTE* src, float* dst, UINT32 frames, UINT32 channels, WORD bitsPerSample)
{
    if (!src || !dst || frames == 0 || channels == 0)
        return false;

    const UINT32 n = frames * channels;

    if (bitsPerSample == 16) {
        const auto* s = reinterpret_cast<const int16_t*>(src);
        for (UINT32 i = 0; i < n; ++i)
            dst[i] = s[i] * (1.0f / 32768.0f);
        return true;
    }

    if (bitsPerSample == 24) {
        // 24-bit packed PCM (3 bytes per sample, little-endian)
        for (UINT32 i = 0; i < n; ++i) {
            int32_t v = static_cast<int32_t>(src[i * 3])
                      | (static_cast<int32_t>(src[i * 3 + 1]) << 8)
                      | (static_cast<int32_t>(src[i * 3 + 2]) << 16);
            // Sign-extend the packed 24-bit value.
            if (v & 0x800000)
                v |= static_cast<int32_t>(0xFF000000);
            dst[i] = v * (1.0f / 8388608.0f);
        }
        return true;
    }

    if (bitsPerSample == 32) {
        const auto* s = reinterpret_cast<const int32_t*>(src);
        for (UINT32 i = 0; i < n; ++i)
            dst[i] = s[i] * (1.0f / 2147483648.0f);
        return true;
    }

    return false;
}

// Downmix an arbitrary source channel layout to stereo.
void downmixToStereo(const float* src, float* dst, UINT32 frames, UINT32 srcChannels)
{
    if (srcChannels == 1) {
        // Mono: duplicate the channel to left and right.
        for (UINT32 i = 0; i < frames; ++i) {
            dst[i * 2] = src[i];
            dst[i * 2 + 1] = src[i];
        }
    } else if (srcChannels == 2) {
        // Stereo: copy interleaved samples directly.
        std::memcpy(dst, src, frames * 2 * sizeof(float));
    } else if (srcChannels == 6) {
        // 5.1 surround: FL, FR, FC, LFE, BL, BR.
        constexpr float kCenter = 0.707f;  // -3dB
        constexpr float kSurround = 0.707f;
        for (UINT32 i = 0; i < frames; ++i) {
            const float* s = src + i * 6;
            dst[i * 2]     = s[0] + kCenter * s[2] + kSurround * s[4]; // L = FL + FC + BL
            dst[i * 2 + 1] = s[1] + kCenter * s[2] + kSurround * s[5]; // R = FR + FC + BR
        }
    } else if (srcChannels == 8) {
        // 7.1 surround: FL, FR, FC, LFE, BL, BR, SL, SR.
        constexpr float kCenter = 0.707f;
        constexpr float kSurround = 0.5f;
        for (UINT32 i = 0; i < frames; ++i) {
            const float* s = src + i * 8;
            dst[i * 2]     = s[0] + kCenter * s[2] + kSurround * (s[4] + s[6]); // L
            dst[i * 2 + 1] = s[1] + kCenter * s[2] + kSurround * (s[5] + s[7]); // R
        }
    } else {
        // Other channel counts: average alternating channels into each side.
        for (UINT32 i = 0; i < frames; ++i) {
            float left = 0.0f, right = 0.0f;
            for (UINT32 ch = 0; ch < srcChannels; ++ch) {
                if (ch % 2 == 0)
                    left += src[i * srcChannels + ch];
                else
                    right += src[i * srcChannels + ch];
            }
            const float leftChannels = (srcChannels + 1) / 2;
            const float rightChannels = srcChannels / 2;
            dst[i * 2] = left / leftChannels;
            dst[i * 2 + 1] = right / rightChannels;
        }
    }
}

// Construct a stereo float32 WAVEFORMATEXTENSIBLE format.
WAVEFORMATEXTENSIBLE makeFloat32Format(DWORD sampleRate, UINT32 channels)
{
    WAVEFORMATEXTENSIBLE fmt = {};
    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = static_cast<WORD>(channels);
    fmt.Format.nSamplesPerSec = sampleRate;
    fmt.Format.wBitsPerSample = 32;
    fmt.Format.nBlockAlign = static_cast<WORD>(channels * sizeof(float));
    fmt.Format.nAvgBytesPerSec = sampleRate * fmt.Format.nBlockAlign;
    fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask = (channels == 1) ? SPEAKER_FRONT_CENTER
                                        : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return fmt;
}

} // namespace

// ---------------------------------------------------------------------------
// WinSession represents one forwarding session. Capture and render each run
// on a worker thread. Each thread creates and owns its COM objects in its own
// MTA, then releases them before exit; no COM pointer crosses apartments.
//
// Realtime design principles:
//   - Strict SPSC RingBuffer ownership (the producer never touches the
//     consumer cursor).
//   - A small capacity based on the source's actual sample rate.
//   - The producer publishes a discontinuity generation; the consumer drops
//     stale queued audio using its own cursor.
//   - Bounded latency takes priority over preserving old audio.
// ---------------------------------------------------------------------------
class WinSession : public std::enable_shared_from_this<WinSession> {
public:
    WinSession(QPointer<AudioRouterWin> owner, QString sourceId, QString targetId, float volume)
        : m_owner(owner),
          m_sourceId(std::move(sourceId)),
          m_targetId(std::move(targetId)),
          m_volumeBits(encodeFloat(std::clamp(volume, 0.0f, kMaxVolume))),
          m_stop(false),
          m_running(false),
          m_captureIsFloat32(true),
          m_captureBitsPerSample(32),
          m_captureChannels(2),
          m_captureSampleRate(0),
          m_ring(nullptr)  // Created after the source sample rate is known.
    {
    }

    ~WinSession()
    {
        requestStop();
        joinThreads();
        if (m_startEvent)
            CloseHandle(m_startEvent);
        if (m_stopEvent)
            CloseHandle(m_stopEvent);
    }

    // Initialize capture first. Its readiness future publishes the source mix
    // format and the sample-rate-sized queue before the render thread starts.
    // Both initialized WASAPI clients wait behind m_startEvent so no queue
    // callback can race partial session initialization.
    bool launch(QString* err)
    {
        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopEvent) {
            *err = winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the stop event"));
            return false;
        }
        m_startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_startEvent) {
            *err = winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the start event"));
            return false;
        }

        std::promise<QString> capReady;
        auto capFuture = capReady.get_future();
        m_capThread = std::thread(&WinSession::captureThreadMain, this, std::move(capReady));

        using namespace std::chrono_literals;
        if (capFuture.wait_for(5s) == std::future_status::timeout) {
            *err = winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Timed out while initializing the source device"));
            requestStop();
            joinThreads();
            return false;
        }
        const QString capErr = capFuture.get();
        if (!capErr.isEmpty()) {
            *err = capErr;
            requestStop();
            joinThreads();
            return false;
        }

        std::promise<QString> renReady;
        auto renFuture = renReady.get_future();
        m_renThread = std::thread(&WinSession::renderThreadMain, this, std::move(renReady));
        if (renFuture.wait_for(5s) == std::future_status::timeout) {
            *err = winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Timed out while initializing the target device"));
            requestStop();
            joinThreads();
            return false;
        }
        const QString renErr = renFuture.get();
        if (!renErr.isEmpty()) {
            *err = renErr;
            requestStop();
            joinThreads();
            return false;
        }

        m_running.store(true, std::memory_order_release);
        if (!SetEvent(m_startEvent)) {
            *err = winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to start the audio worker threads"));
            requestStop();
            joinThreads();
            return false;
        }
        return true;
    }

    void requestStop()
    {
        m_running.store(false, std::memory_order_release);
        m_stop.store(true, std::memory_order_release);
        if (m_stopEvent)
            SetEvent(m_stopEvent);
    }

    void joinThreads()
    {
        if (m_capThread.joinable()
            && m_capThread.get_id() != std::this_thread::get_id())
            m_capThread.join();
        if (m_renThread.joinable()
            && m_renThread.get_id() != std::this_thread::get_id())
            m_renThread.join();
    }

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    bool usesDevice(const QString& id) const { return m_sourceId == id || m_targetId == id; }
    void setVolume(float v)
    {
        if (!std::isfinite(v))
            return;
        m_volumeBits.store(encodeFloat(std::clamp(v, 0.0f, kMaxVolume)),
                           std::memory_order_relaxed);
    }

    SessionDeviceIds deviceIds() const
    {
        return { m_sourceId, m_targetId };
    }

private:
    // Post an error to the GUI thread. Queued work is dropped if the owner is
    // already being destroyed.
    void postError(const QString& msg)
    {
        QPointer<AudioRouterWin> owner = m_owner;
        if (!owner)
            return;
        std::weak_ptr<WinSession> session = weak_from_this();
        QMetaObject::invokeMethod(
            owner,
            [owner, session, msg]() {
                const std::shared_ptr<WinSession> liveSession = session.lock();
                if (owner && liveSession)
                    owner->notifyThreadError(liveSession.get(), msg);
            },
            Qt::QueuedConnection);
    }

    bool waitForSessionStart(const char* workerName)
    {
        HANDLE waits[2] = { m_stopEvent, m_startEvent };
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1
            && !m_stop.load(std::memory_order_acquire)) {
            return true;
        }
        if (result == WAIT_FAILED) {
            requestStop();
            postError(winTr(QT_TRANSLATE_NOOP(
                          "AudioRouterWin", "The %1 worker failed to start; monitoring stopped"))
                          .arg(winTr(workerName)));
        }
        return false;
    }

    void captureThreadMain(std::promise<QString> ready)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            ready.set_value(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to initialize capture-thread COM (%1)"))
                                .arg(hresultText(comResult)));
            return;
        }
        MmcssRegistration mmcss;
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> cap;
        HANDLE event = nullptr;
        WAVEFORMATEX* mixFmt = nullptr;
        UINT32 captureBufferFrames = 0;
        std::vector<float> convBuf;
        std::vector<float> downmixBuf;

        auto finish = [&]() {
            if (event)
                CloseHandle(event);
            if (mixFmt)
                CoTaskMemFree(mixFmt);
            cap.reset();
            client.reset();
            device.reset();
            enumerator.reset();
            mmcss.reset();
            CoUninitialize();
        };
        // Common initialization-failure path: publish the error before cleanup.
        auto fail = [&](const QString& err) {
            ready.set_value(err);
            finish();
        };

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create MMDeviceEnumerator (%1)"))
                            .arg(hresultText(hr)));
        hr = enumerator->GetDevice(qToWide(m_sourceId).c_str(), device.put());
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Source device was not found: %1 (%2)"))
                            .arg(m_sourceId, hresultText(hr)));
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(client.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to activate the source device (%1)"))
                            .arg(hresultText(hr)));
        hr = client->GetMixFormat(&mixFmt);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to obtain the source mix format (%1)"))
                            .arg(hresultText(hr)));

        // Validate the source format before publishing it to the render thread.
        m_captureIsFloat32 = isFloat32Format(mixFmt);
        m_captureBitsPerSample = mixFmt->wBitsPerSample;
        m_captureChannels = mixFmt->nChannels;
        m_captureSampleRate = mixFmt->nSamplesPerSec;

        if (m_captureChannels == 0 || m_captureChannels > 32) {
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "The source device reported an invalid channel count: %1"))
                            .arg(m_captureChannels));
        }
        if (m_captureSampleRate == 0)
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "The source device reported an invalid sample rate")));
        if (!m_captureIsFloat32 && !isSupportedIntegerPcmFormat(mixFmt)) {
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "The source format is unsupported (%1-bit PCM, %2 channels)"))
                            .arg(m_captureBitsPerSample)
                            .arg(m_captureChannels));
        }

        // Queue storage is configured from the source's actual mix rate. The
        // duration is a physical capacity, not an end-to-end latency report.
        try {
            m_ring = std::make_unique<RingBuffer>(
                kChannels, m_captureSampleRate, kQueueBufferDurationMs);
        } catch (const std::exception& ex) {
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the audio queue: %1"))
                            .arg(QString::fromLocal8Bit(ex.what())));
        }

        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the capture event")));
        // Loopback capture must use the device mix format. Shared mode plus
        // LOOPBACK exposes the audio currently being played.
        // Shared event-driven streams require both timing arguments to be 0;
        // GetBufferSize below is the resulting WASAPI capacity.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, mixFmt, nullptr);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin",
                "Loopback capture initialization failed (%1). The device may be unavailable or held in exclusive mode."))
                            .arg(hresultText(hr)));
        hr = client->SetEventHandle(event);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "SetEventHandle failed (%1)"))
                            .arg(hresultText(hr)));
        hr = client->GetBufferSize(&captureBufferFrames);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to obtain the capture buffer size (%1)"))
                            .arg(hresultText(hr)));
        hr = client->GetService(IID_PPV_ARGS(cap.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to obtain the capture client (%1)"))
                            .arg(hresultText(hr)));

        // Allocate conversion storage before the event loops begin. The
        // WASAPI buffer capacity bounds every packet returned by GetBuffer().
        try {
            if (!m_captureIsFloat32) {
                convBuf.resize(static_cast<size_t>(captureBufferFrames)
                               * m_captureChannels);
            }
            if (m_captureChannels != kChannels) {
                downmixBuf.resize(static_cast<size_t>(captureBufferFrames)
                                  * kChannels);
            }
        } catch (const std::exception& ex) {
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to allocate capture conversion buffers: %1"))
                            .arg(QString::fromLocal8Bit(ex.what())));
        }

        hr = client->Start();
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to start capture (%1)"))
                            .arg(hresultText(hr)));

        ready.set_value(QString()); // Initialization succeeded.
        mmcss.registerCurrentThread();
        if (!waitForSessionStart(QT_TRANSLATE_NOOP("AudioRouterWin", "Capture"))) {
            client->Stop();
            finish();
            return;
        }

        {
            HANDLE waits[2] = { m_stopEvent, event };
            for (;;) {
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0 || m_stop.load(std::memory_order_acquire))
                    break; // Normal stop.
                if (r == WAIT_FAILED) {
                    if (!m_stop.load(std::memory_order_acquire)) {
                        requestStop();
                        postError(winTr(QT_TRANSLATE_NOOP(
                            "AudioRouterWin",
                            "Waiting for the capture event failed; monitoring stopped")));
                    }
                    break;
                }
                if (r != WAIT_OBJECT_0 + 1)
                    continue;
                UINT32 packet = 0;
                bool failed = false;
                for (;;) {
                    hr = cap->GetNextPacketSize(&packet);
                    if (FAILED(hr)) {
                        failed = true;
                        if (!m_stop.load(std::memory_order_acquire)) {
                            requestStop();
                            postError(winTr(QT_TRANSLATE_NOOP(
                                "AudioRouterWin",
                                "The source device became unavailable; monitoring stopped")));
                        }
                        break;
                    }
                    if (packet == 0)
                        break;
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                    if (FAILED(hr)) {
                        failed = true;
                        if (!m_stop.load(std::memory_order_acquire)) {
                            requestStop();
                            postError(winTr(QT_TRANSLATE_NOOP(
                                "AudioRouterWin",
                                "Reading capture data failed; monitoring stopped")));
                        }
                        break;
                    }

                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                        m_ring->signalDiscontinuity();

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        m_ring->writeSilence(frames);
                    } else {
                        const float* floatData = nullptr;

                        // Step 1: convert the source format when required.
                        if (m_captureIsFloat32) {
                            floatData = reinterpret_cast<const float*>(data);
                        } else {
                            if (!convertPcmToFloat32(data, convBuf.data(), frames, m_captureChannels, m_captureBitsPerSample)) {
                                // Skip a packet that cannot be converted.
                                cap->ReleaseBuffer(frames);
                                continue;
                            }
                            floatData = convBuf.data();
                        }

                        // Step 2: downmix to stereo when required.
                        const float* finalData = floatData;
                        if (m_captureChannels != kChannels) {
                            downmixToStereo(floatData, downmixBuf.data(), frames, m_captureChannels);
                            finalData = downmixBuf.data();
                        }

                        // A partial result is still frame-aligned. RingBuffer
                        // publishes a discontinuity for the dropped suffix.
                        m_ring->write(finalData, frames);
                    }

                    if (FAILED(cap->ReleaseBuffer(frames))) {
                        failed = true;
                        if (!m_stop.load(std::memory_order_acquire)) {
                            requestStop();
                            postError(winTr(QT_TRANSLATE_NOOP(
                                "AudioRouterWin",
                                "Releasing capture data failed; monitoring stopped")));
                        }
                        break;
                    }
                }
                if (failed)
                    break;
            }
        }

        client->Stop();
        finish();
    }

    void renderThreadMain(std::promise<QString> ready)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            ready.set_value(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to initialize render-thread COM (%1)"))
                                .arg(hresultText(comResult)));
            return;
        }
        MmcssRegistration mmcss;
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> ren;
        std::unique_ptr<AdaptiveAudioBufferReader> queueReader;
        HANDLE event = nullptr;
        UINT32 bufferFrames = 0;

        auto cleanup = [&]() {
            if (event)
                CloseHandle(event);
            ren.reset();
            client.reset();
            device.reset();
            enumerator.reset();
            mmcss.reset();
            CoUninitialize();
        };
        auto fail = [&](const QString& err) {
            ready.set_value(err);
            cleanup();
        };

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create MMDeviceEnumerator (%1)"))
                            .arg(hresultText(hr)));

        // captureThreadMain published this value through capReady before this
        // thread was created, so the render format and queue use one rate.
        const WAVEFORMATEXTENSIBLE renderFmt =
            makeFloat32Format(m_captureSampleRate, kChannels);

        hr = enumerator->GetDevice(qToWide(m_targetId).c_str(), device.put());
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Target device was not found: %1 (%2)"))
                            .arg(m_targetId, hresultText(hr)));
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(client.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to activate the target device (%1)"))
                            .arg(hresultText(hr)));
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the render event")));
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                            | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        // Shared event-driven streams require both timing arguments to be 0;
        // bufferFrames is queried after initialization rather than assumed.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                               &renderFmt.Format, nullptr);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "The target device rejected the audio format (%1)"))
                            .arg(hresultText(hr)));
        hr = client->GetBufferSize(&bufferFrames);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to obtain the render buffer size (%1)"))
                            .arg(hresultText(hr)));
        hr = client->SetEventHandle(event);
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "SetEventHandle failed (%1)"))
                            .arg(hresultText(hr)));
        hr = client->GetService(IID_PPV_ARGS(ren.put()));
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to obtain the render client (%1)"))
                            .arg(hresultText(hr)));

        // Keep a short safety margin between the independent source and
        // target clocks. The target accounts for the largest render request,
        // while retaining queue headroom for scheduling jitter. Outside the
        // hysteresis window the reader corrects drift by at most one frame
        // per callback instead of waiting for a destructive overflow flush.
        const size_t queueCapacity = m_ring->capacity();
        const size_t hysteresisFrames = std::max<size_t>(
            1, framesForDuration(m_captureSampleRate,
                                 kQueueHysteresisDurationMs));
        const size_t headroomFrames = std::max(
            hysteresisFrames,
            framesForDuration(m_captureSampleRate, kQueueSafetyDurationMs / 2));
        const size_t maxTargetFrames = queueCapacity > headroomFrames
            ? queueCapacity - headroomFrames
            : queueCapacity;
        const size_t minimumForRender = std::min<size_t>(bufferFrames, queueCapacity);
        const size_t desiredTargetFrames = std::max(
            framesForDuration(m_captureSampleRate, kQueueTargetDurationMs),
            static_cast<size_t>(bufferFrames)
                + framesForDuration(m_captureSampleRate,
                                    kQueueSafetyDurationMs));
        const size_t targetFrames = std::max(
            minimumForRender,
            std::min(desiredTargetFrames, maxTargetFrames));
        try {
            queueReader = std::make_unique<AdaptiveAudioBufferReader>(
                *m_ring, bufferFrames, targetFrames, hysteresisFrames);
        } catch (const std::exception& ex) {
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to create the adaptive audio reader: %1"))
                            .arg(QString::fromLocal8Bit(ex.what())));
        }

        hr = client->Start();
        if (FAILED(hr))
            return fail(winTr(QT_TRANSLATE_NOOP(
                "AudioRouterWin", "Unable to start rendering (%1)"))
                            .arg(hresultText(hr)));

        ready.set_value(QString()); // Initialization succeeded.
        mmcss.registerCurrentThread();
        if (!waitForSessionStart(QT_TRANSLATE_NOOP("AudioRouterWin", "Render"))) {
            client->Stop();
            cleanup();
            return;
        }

        {
            HANDLE waits[2] = { m_stopEvent, event };
            for (;;) {
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0 || m_stop.load(std::memory_order_acquire))
                    break;
                if (r == WAIT_FAILED) {
                    if (!m_stop.load(std::memory_order_acquire)) {
                        requestStop();
                        postError(winTr(QT_TRANSLATE_NOOP(
                            "AudioRouterWin",
                            "Waiting for the render event failed; monitoring stopped")));
                    }
                    break;
                }
                if (r != WAIT_OBJECT_0 + 1)
                    continue;

                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding))) {
                    if (!m_stop.load(std::memory_order_acquire)) {
                        requestStop();
                        postError(winTr(QT_TRANSLATE_NOOP(
                            "AudioRouterWin",
                            "The target device became unavailable; monitoring stopped")));
                    }
                    break;
                }
                if (padding >= bufferFrames)
                    continue;

                const UINT32 avail = bufferFrames - padding;
                BYTE* data = nullptr;
                if (FAILED(ren->GetBuffer(avail, &data))) {
                    if (!m_stop.load(std::memory_order_acquire)) {
                        requestStop();
                        postError(winTr(QT_TRANSLATE_NOOP(
                            "AudioRouterWin",
                            "Obtaining the render buffer failed; monitoring stopped")));
                    }
                    break;
                }

                const size_t consumedFrames = queueReader->render(
                    *m_ring,
                    reinterpret_cast<float*>(data), avail,
                    decodeFloat(m_volumeBits.load(std::memory_order_relaxed)));
                const DWORD releaseFlags = consumedFrames == 0
                    ? AUDCLNT_BUFFERFLAGS_SILENT : 0;
                if (FAILED(ren->ReleaseBuffer(avail, releaseFlags))) {
                    if (!m_stop.load(std::memory_order_acquire)) {
                        requestStop();
                        postError(winTr(QT_TRANSLATE_NOOP(
                            "AudioRouterWin",
                            "Submitting the render buffer failed; monitoring stopped")));
                    }
                    break;
                }
            }
        }

        client->Stop();
        cleanup();
    }

    QPointer<AudioRouterWin> m_owner;
    QString m_sourceId;
    QString m_targetId;
    // Published by capReady before render thread creation. Destroyed only after
    // both producer and consumer threads have joined.
    std::unique_ptr<RingBuffer> m_ring;
    std::atomic<uint32_t> m_volumeBits;
    std::atomic<bool> m_stop;
    std::atomic<bool> m_running;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_startEvent = nullptr;
    std::thread m_capThread;
    std::thread m_renThread;

    // Capture format published by the capture worker before render startup.
    bool m_captureIsFloat32;
    WORD m_captureBitsPerSample;
    UINT32 m_captureChannels;
    DWORD m_captureSampleRate;
};

// ---------------------------------------------------------------------------
// NotificationClient watches for device hotplug and default-device changes.
// MMDevice callbacks may arrive off the GUI thread; all router work is queued.
// ---------------------------------------------------------------------------
class NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(AudioRouterWin* owner) : m_owner(owner), m_ref(1) {}

    void detachOwner() noexcept
    {
        m_owner.store(nullptr, std::memory_order_release);
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override
    {
        const QString devId = wideToQString(id);
        post([devId, state](AudioRouterWin* owner) {
            owner->notifyDevicesChanged();
            if (state != DEVICE_STATE_ACTIVE)
                owner->notifyDeviceGone(devId);
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
    {
        post([](AudioRouterWin* owner) { owner->notifyDevicesChanged(); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override
    {
        const QString devId = wideToQString(id);
        post([devId](AudioRouterWin* owner) {
            owner->notifyDevicesChanged();
            owner->notifyDeviceGone(devId);
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override
    {
        post([](AudioRouterWin* owner) { owner->notifyDevicesChanged(); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override
    {
        return S_OK;
    }

private:
    template <typename Functor>
    void post(Functor&& fn)
    {
        AudioRouterWin* rawOwner = m_owner.load(std::memory_order_acquire);
        if (!rawOwner)
            return;
        QPointer<AudioRouterWin> owner(rawOwner);
        if (!owner)
            return;
        // The queued closure does not retain NotificationClient. It remains
        // safe after unregister/release and is guarded against router deletion.
        QMetaObject::invokeMethod(
            owner,
            [owner, fn = std::forward<Functor>(fn)]() mutable {
                if (owner)
                    fn(owner.data());
            },
            Qt::QueuedConnection);
    }

    std::atomic<AudioRouterWin*> m_owner;
    LONG m_ref;
};

// ---------------------------------------------------------------------------
// AudioRouterWin
// ---------------------------------------------------------------------------
AudioRouterWin::AudioRouterWin(QObject* parent) : AudioRouter(parent)
{
    m_notifier = new NotificationClient(this);
    ComPtr<IMMDeviceEnumerator> enumerator;
    const HRESULT createResult = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(enumerator.put()));
    if (SUCCEEDED(createResult)) {
        const HRESULT registerResult =
            enumerator->RegisterEndpointNotificationCallback(m_notifier);
        if (SUCCEEDED(registerResult)) {
            m_notificationEnumerator = enumerator.get();
            m_notificationEnumerator->AddRef();
            m_notifierRegistered = true;
        } else {
            qWarning("RegisterEndpointNotificationCallback failed: 0x%08lX",
                     static_cast<unsigned long>(registerResult));
        }
    } else {
        qWarning("Unable to create notification MMDeviceEnumerator: 0x%08lX",
                 static_cast<unsigned long>(createResult));
    }
}

AudioRouterWin::~AudioRouterWin()
{
    stop();
    if (m_notifier) {
        // Prevent new queued work first, then unregister on the exact COM
        // enumerator instance used for registration.
        m_notifier->detachOwner();
        if (m_notifierRegistered && m_notificationEnumerator) {
            const HRESULT unregisterResult =
                m_notificationEnumerator->UnregisterEndpointNotificationCallback(m_notifier);
            if (FAILED(unregisterResult)) {
                qWarning("UnregisterEndpointNotificationCallback failed: 0x%08lX",
                         static_cast<unsigned long>(unregisterResult));
            }
            m_notifierRegistered = false;
        }
        if (m_notificationEnumerator) {
            m_notificationEnumerator->Release();
            m_notificationEnumerator = nullptr;
        }
        m_notifier->Release();
        m_notifier = nullptr;
    }
}

QVector<DeviceInfo> AudioRouterWin::outputDevices()
{
    QVector<DeviceInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.put()))))
        return result;

    QString defaultId;
    {
        ComPtr<IMMDevice> def;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, def.put()))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(def->GetId(&id))) {
                defaultId = wideToQString(id);
                CoTaskMemFree(id);
            }
        }
    }

    ComPtr<IMMDeviceCollection> coll;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, coll.put())))
        return result;
    UINT count = 0;
    if (FAILED(coll->GetCount(&count)))
        return result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, dev.put())))
            continue;
        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id)))
            continue;
        const QString devId = wideToQString(id);
        CoTaskMemFree(id);

        DeviceInfo info;
        info.id = devId;
        info.isDefault = (devId == defaultId);

        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, store.put()))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                info.name = wideToQString(v.pwszVal);
            PropVariantClear(&v);
        }
        if (info.name.isEmpty())
            info.name = winTr(QT_TRANSLATE_NOOP("AudioRouterWin", "Output device %1"))
                            .arg(i + 1);
        result.append(info);
    }
    return result;
}

bool AudioRouterWin::start(const QString& sourceId, const QString& targetId, float volume)
{
    if (!std::isfinite(volume)) {
        emit errorOccurred(winTr(QT_TRANSLATE_NOOP(
            "AudioRouterWin", "Monitoring volume must be a finite number")));
        return false;
    }
    if (sourceId.isEmpty() || targetId.isEmpty()) {
        emit errorOccurred(winTr(QT_TRANSLATE_NOOP(
            "AudioRouterWin", "The source and target must be valid devices")));
        return false;
    }
    if (sourceId == targetId) {
        emit errorOccurred(winTr(QT_TRANSLATE_NOOP(
            "AudioRouterWin",
            "The source and target must be different to prevent an audio feedback loop")));
        return false;
    }
    stop();
    auto session = std::make_shared<WinSession>(this, sourceId, targetId, volume);
    QString err;
    if (!session->launch(&err)) {
        emit errorOccurred(err);
        return false;
    }
    m_session = std::move(session);
    // Capture the IDs only after both WASAPI workers have initialized. This
    // snapshot is independent of subsequent GUI device-list refreshes.
    m_lastSessionDeviceIds = { sourceId, targetId };
    emit started();
    return true;
}

void AudioRouterWin::stop()
{
    if (!m_session) {
        m_lastSessionDeviceIds = {};
        return;
    }
    stopSession(QString(), StopReason::UserRequested);
}

void AudioRouterWin::stopSession(const QString& reason, StopReason stopReason)
{
    if (!m_session)
        return;
    const SessionDeviceIds sessionIds = m_session->deviceIds();
    m_session->requestStop();
    m_session->joinThreads();
    m_session.reset();
    if (stopReason == StopReason::DeviceFailure || stopReason == StopReason::ServiceFailure)
        m_lastSessionDeviceIds = sessionIds;
    else
        m_lastSessionDeviceIds = {};
    if (!reason.isEmpty())
        emit errorOccurred(reason);
    emit stopped(stopReason);
}

bool AudioRouterWin::isRunning() const
{
    return m_session && m_session->isRunning();
}

SessionDeviceIds AudioRouterWin::lastSessionDeviceIds() const
{
    return m_lastSessionDeviceIds;
}

void AudioRouterWin::setVolume(float volume)
{
    if (m_session)
        m_session->setVolume(volume);
}

void AudioRouterWin::notifyThreadError(WinSession* session, const QString& message)
{
    if (m_session && m_session.get() == session)
        stopSession(message, StopReason::DeviceFailure);
}

void AudioRouterWin::notifyDevicesChanged()
{
    emit deviceListChanged();
}

void AudioRouterWin::notifyDeviceGone(const QString& id)
{
    if (m_session && m_session->usesDevice(id))
        stopSession(winTr(QT_TRANSLATE_NOOP(
                        "AudioRouterWin", "The audio device in use was removed or disabled")),
                    StopReason::DeviceFailure);
}
