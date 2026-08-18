#include "audiorouter_win.h"

// windows.h 的 min/max 宏会破坏 ringbuffer.h 中的 std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

#include "ringbuffer.h"

#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// 极简 COM 智能指针（不依赖 ATL，兼容 MSVC 与 MinGW）。
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
    return QString::fromWCharArray(w);
}

std::wstring qToWide(const QString& s)
{
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()));
}

std::string hresultText(HRESULT hr)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buf);
}

constexpr UINT32 kChannels = 2;
constexpr size_t kQueueBufferDurationMs = 50;

// 检查 WAVEFORMATEX 是否为 32-bit IEEE float 格式
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

// 将整型 PCM 转换为 float32（范围 [-1.0, 1.0]）
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
            // 符号扩展
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

// 将多声道音频降混音到立体声
void downmixToStereo(const float* src, float* dst, UINT32 frames, UINT32 srcChannels)
{
    if (srcChannels == 1) {
        // 单声道：复制到左右声道
        for (UINT32 i = 0; i < frames; ++i) {
            dst[i * 2] = src[i];
            dst[i * 2 + 1] = src[i];
        }
    } else if (srcChannels == 2) {
        // 立体声：直接复制
        std::memcpy(dst, src, frames * 2 * sizeof(float));
    } else if (srcChannels == 6) {
        // 5.1 环绕声：FL, FR, FC, LFE, BL, BR
        constexpr float kCenter = 0.707f;  // -3dB
        constexpr float kSurround = 0.707f;
        for (UINT32 i = 0; i < frames; ++i) {
            const float* s = src + i * 6;
            dst[i * 2]     = s[0] + kCenter * s[2] + kSurround * s[4]; // L = FL + FC + BL
            dst[i * 2 + 1] = s[1] + kCenter * s[2] + kSurround * s[5]; // R = FR + FC + BR
        }
    } else if (srcChannels == 8) {
        // 7.1 环绕声：FL, FR, FC, LFE, BL, BR, SL, SR
        constexpr float kCenter = 0.707f;
        constexpr float kSurround = 0.5f;
        for (UINT32 i = 0; i < frames; ++i) {
            const float* s = src + i * 8;
            dst[i * 2]     = s[0] + kCenter * s[2] + kSurround * (s[4] + s[6]); // L
            dst[i * 2 + 1] = s[1] + kCenter * s[2] + kSurround * (s[5] + s[7]); // R
        }
    } else {
        // 其他声道数：简单平均分配
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

// 构造 float32 WAVEFORMATEXTENSIBLE
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
// WinSession：一次转发会话。捕获/播放各一个工作线程。
// 每个线程在自己的 MTA 中创建并持有各自的 COM 对象，退出时释放，
// 避免跨线程公寓（STA/MTA）使用 COM 指针的问题。
//
// OBS 风格实时监听原则：
//   - 使用严格 SPSC RingBuffer（生产者不触碰消费者游标）
//   - 小容量设计（基于源设备真实采样率，而非"尽可能多缓冲"）
//   - 生产者发布溢出代次，消费者用自己的游标丢弃陈旧数据
//   - 低延迟 > 音频完整性
// ---------------------------------------------------------------------------
class WinSession : public std::enable_shared_from_this<WinSession> {
public:
    WinSession(QPointer<AudioRouterWin> owner, QString sourceId, QString targetId, float volume)
        : m_owner(owner),
          m_sourceId(std::move(sourceId)),
          m_targetId(std::move(targetId)),
          m_volume(std::clamp(volume, 0.0f, 2.0f)),
          m_stop(false),
          m_running(false),
          m_captureIsFloat32(true),
          m_captureBitsPerSample(32),
          m_captureChannels(2),
          m_captureSampleRate(0),
          m_ring(nullptr)  // Will be created after discovering sample rate
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
    bool launch(std::string* err)
    {
        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopEvent) {
            *err = "无法创建事件对象";
            return false;
        }
        m_startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_startEvent) {
            *err = "无法创建启动事件对象";
            return false;
        }

        std::promise<std::string> capReady;
        auto capFuture = capReady.get_future();
        m_capThread = std::thread(&WinSession::captureThreadMain, this, std::move(capReady));

        using namespace std::chrono_literals;
        if (capFuture.wait_for(5s) == std::future_status::timeout) {
            *err = "初始化监听源设备超时";
            requestStop();
            joinThreads();
            return false;
        }
        const std::string capErr = capFuture.get();
        if (!capErr.empty()) {
            *err = capErr;
            requestStop();
            joinThreads();
            return false;
        }

        std::promise<std::string> renReady;
        auto renFuture = renReady.get_future();
        m_renThread = std::thread(&WinSession::renderThreadMain, this, std::move(renReady));
        if (renFuture.wait_for(5s) == std::future_status::timeout) {
            *err = "初始化转发目标设备超时";
            requestStop();
            joinThreads();
            return false;
        }
        const std::string renErr = renFuture.get();
        if (!renErr.empty()) {
            *err = renErr;
            requestStop();
            joinThreads();
            return false;
        }

        m_running.store(true, std::memory_order_release);
        if (!SetEvent(m_startEvent)) {
            *err = "无法启动音频工作线程";
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
        m_volume.store(std::clamp(v, 0.0f, 2.0f), std::memory_order_relaxed);
    }

private:
    // 把错误投递回 GUI 线程（m_owner 销毁后自动丢弃）。
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

    bool waitForSessionStart(const QString& workerName)
    {
        HANDLE waits[2] = { m_stopEvent, m_startEvent };
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1
            && !m_stop.load(std::memory_order_acquire)) {
            return true;
        }
        if (result == WAIT_FAILED) {
            requestStop();
            postError(QStringLiteral("等待%1启动失败，监听已停止").arg(workerName));
        }
        return false;
    }

    void captureThreadMain(std::promise<std::string> ready)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            ready.set_value("无法初始化捕获线程 COM（" + hresultText(comResult) + "）");
            return;
        }
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
            CoUninitialize();
        };
        // 初始化失败的公共出口：先交付错误，再做清理。
        auto fail = [&](const std::string& err) {
            ready.set_value(err);
            finish();
        };

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.put()));
        if (FAILED(hr))
            return fail("无法创建 MMDeviceEnumerator（" + hresultText(hr) + "）");
        hr = enumerator->GetDevice(qToWide(m_sourceId).c_str(), device.put());
        if (FAILED(hr))
            return fail("找不到监听源设备：" + m_sourceId.toStdString());
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(client.put()));
        if (FAILED(hr))
            return fail("无法激活监听源设备（" + hresultText(hr) + "）");
        if (FAILED(client->GetMixFormat(&mixFmt)))
            return fail("无法获取监听源的混音格式");

        // Validate the source format before publishing it to the render thread.
        m_captureIsFloat32 = isFloat32Format(mixFmt);
        m_captureBitsPerSample = mixFmt->wBitsPerSample;
        m_captureChannels = mixFmt->nChannels;
        m_captureSampleRate = mixFmt->nSamplesPerSec;

        if (m_captureChannels == 0 || m_captureChannels > 32) {
            return fail("监听源设备声道数异常：" + std::to_string(m_captureChannels));
        }
        if (m_captureSampleRate == 0)
            return fail("监听源设备采样率异常");
        if (!m_captureIsFloat32 && !isSupportedIntegerPcmFormat(mixFmt)) {
            return fail("监听源设备格式不受支持（" + std::to_string(m_captureBitsPerSample)
                        + "-bit PCM, " + std::to_string(m_captureChannels) + " 声道）");
        }

        // Queue storage is configured from the source's actual mix rate. The
        // duration is a physical capacity, not an end-to-end latency report.
        try {
            m_ring = std::make_unique<RingBuffer>(
                kChannels, m_captureSampleRate, kQueueBufferDurationMs);
        } catch (const std::exception& ex) {
            return fail("无法创建音频队列：" + std::string(ex.what()));
        }

        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return fail("无法创建捕获事件");
        // 回环捕获必须使用设备混音格式；共享模式 + LOOPBACK 得到"正在播放"的流。
        // Shared event-driven streams require both timing arguments to be 0;
        // GetBufferSize below is the resulting WASAPI capacity.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, mixFmt, nullptr);
        if (FAILED(hr))
            return fail("初始化回环捕获失败（" + hresultText(hr)
                        + "）。设备可能被独占模式占用或不可用。");
        if (FAILED(client->SetEventHandle(event)))
            return fail("SetEventHandle 失败");
        if (FAILED(client->GetBufferSize(&captureBufferFrames)))
            return fail("获取捕获缓冲区大小失败");
        if (FAILED(client->GetService(IID_PPV_ARGS(cap.put()))))
            return fail("获取捕获客户端失败");

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
            return fail("无法分配捕获转换缓冲区：" + std::string(ex.what()));
        }

        if (FAILED(client->Start()))
            return fail("启动捕获失败");

        ready.set_value(std::string()); // 初始化成功，通知主线程
        if (!waitForSessionStart(QStringLiteral("捕获线程"))) {
            client->Stop();
            finish();
            return;
        }

        {
            HANDLE waits[2] = { m_stopEvent, event };
            for (;;) {
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0 || m_stop.load(std::memory_order_acquire))
                    break; // 正常停止
                if (r == WAIT_FAILED) {
                    requestStop();
                    postError(QStringLiteral("等待捕获事件失败，监听已停止"));
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
                            postError(QStringLiteral("监听源设备已失效，监听已停止"));
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
                            postError(QStringLiteral("读取监听源数据失败，监听已停止"));
                        }
                        break;
                    }

                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                        m_ring->signalDiscontinuity();

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        m_ring->writeSilence(frames);
                    } else {
                        const float* floatData = nullptr;

                        // 步骤1：格式转换（如需要）
                        if (m_captureIsFloat32) {
                            floatData = reinterpret_cast<const float*>(data);
                        } else {
                            if (!convertPcmToFloat32(data, convBuf.data(), frames, m_captureChannels, m_captureBitsPerSample)) {
                                // 转换失败，跳过此包
                                cap->ReleaseBuffer(frames);
                                continue;
                            }
                            floatData = convBuf.data();
                        }

                        // 步骤2：声道降混音（如需要）
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
                        requestStop();
                        postError(QStringLiteral("释放监听源数据失败，监听已停止"));
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

    void renderThreadMain(std::promise<std::string> ready)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            ready.set_value("无法初始化播放线程 COM（" + hresultText(comResult) + "）");
            return;
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> ren;
        HANDLE event = nullptr;
        UINT32 bufferFrames = 0;

        auto cleanup = [&]() {
            if (event)
                CloseHandle(event);
            ren.reset();
            client.reset();
            device.reset();
            enumerator.reset();
            CoUninitialize();
        };
        auto fail = [&](const std::string& err) {
            ready.set_value(err);
            cleanup();
        };

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.put()));
        if (FAILED(hr))
            return fail("无法创建 MMDeviceEnumerator（" + hresultText(hr) + "）");

        // captureThreadMain published this value through capReady before this
        // thread was created, so the render format and queue use one rate.
        const WAVEFORMATEXTENSIBLE renderFmt =
            makeFloat32Format(m_captureSampleRate, kChannels);

        if (FAILED(enumerator->GetDevice(qToWide(m_targetId).c_str(), device.put())))
            return fail("找不到转发目标设备：" + m_targetId.toStdString());
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(client.put()))))
            return fail("无法激活转发目标设备");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return fail("无法创建播放事件");
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                            | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        // Shared event-driven streams require both timing arguments to be 0;
        // bufferFrames is queried after initialization rather than assumed.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                               &renderFmt.Format, nullptr);
        if (FAILED(hr))
            return fail("转发目标设备不支持该音频格式（" + hresultText(hr) + "）");
        if (FAILED(client->GetBufferSize(&bufferFrames)))
            return fail("获取播放缓冲区大小失败");
        if (FAILED(client->SetEventHandle(event)))
            return fail("SetEventHandle 失败");
        if (FAILED(client->GetService(IID_PPV_ARGS(ren.put()))))
            return fail("获取播放客户端失败");
        if (FAILED(client->Start()))
            return fail("启动播放失败");

        ready.set_value(std::string()); // 初始化成功
        if (!waitForSessionStart(QStringLiteral("播放线程"))) {
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
                    requestStop();
                    postError(QStringLiteral("等待播放事件失败，监听已停止"));
                    break;
                }
                if (r != WAIT_OBJECT_0 + 1)
                    continue;

                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding))) {
                    requestStop();
                    postError(QStringLiteral("转发目标设备已失效，监听已停止"));
                    break;
                }
                if (padding >= bufferFrames)
                    continue;

                const UINT32 avail = bufferFrames - padding;
                BYTE* data = nullptr;
                if (FAILED(ren->GetBuffer(avail, &data))) {
                    requestStop();
                    postError(QStringLiteral("获取转发目标缓冲区失败，监听已停止"));
                    break;
                }

                const size_t readFrames = m_ring->read(
                    reinterpret_cast<float*>(data), avail,
                    m_volume.load(std::memory_order_relaxed));
                const DWORD releaseFlags = readFrames == 0
                    ? AUDCLNT_BUFFERFLAGS_SILENT : 0;
                if (FAILED(ren->ReleaseBuffer(avail, releaseFlags))) {
                    requestStop();
                    postError(QStringLiteral("提交转发目标缓冲区失败，监听已停止"));
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
    std::atomic<float> m_volume;
    std::atomic<bool> m_stop;
    std::atomic<bool> m_running;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_startEvent = nullptr;
    std::thread m_capThread;
    std::thread m_renThread;

    // 捕获格式信息
    bool m_captureIsFloat32;
    WORD m_captureBitsPerSample;
    UINT32 m_captureChannels;
    DWORD m_captureSampleRate;
};

// ---------------------------------------------------------------------------
// NotificationClient：监听设备热插拔/默认设备变化。
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
            info.name = QStringLiteral("输出设备 %1").arg(i + 1);
        result.append(info);
    }
    return result;
}

bool AudioRouterWin::start(const QString& sourceId, const QString& targetId, float volume)
{
    if (sourceId == targetId) {
        emit errorOccurred(QStringLiteral(
            "监听源和转发目标不能是同一设备，否则会形成音频反馈回路"));
        return false;
    }
    stop();
    auto session = std::make_shared<WinSession>(this, sourceId, targetId, volume);
    std::string err;
    if (!session->launch(&err)) {
        emit errorOccurred(QString::fromStdString(err));
        return false;
    }
    m_session = std::move(session);
    emit started();
    return true;
}

void AudioRouterWin::stop()
{
    if (!m_session)
        return;
    stopSession(QString(), StopReason::UserRequested);
}

void AudioRouterWin::stopSession(const QString& reason, StopReason stopReason)
{
    if (!m_session)
        return;
    m_session->requestStop();
    m_session->joinThreads();
    m_session.reset();
    if (!reason.isEmpty())
        emit errorOccurred(reason);
    emit stopped(stopReason);
}

bool AudioRouterWin::isRunning() const
{
    return m_session && m_session->isRunning();
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
        stopSession(QStringLiteral("正在使用的音频设备已移除或停用"), StopReason::DeviceFailure);
}
