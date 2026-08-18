#pragma once

#include "audiorouter.h"

#include <memory>

class WinSession;
class NotificationClient;
struct IMMDeviceEnumerator;

// Windows 实现：
//  - 监听源：对所选输出设备开启 WASAPI 回环捕获（AUDCLNT_STREAMFLAGS_LOOPBACK），
//    拿到"正在播放"的音频；
//  - 转发目标：另一个输出设备的 WASAPI 共享模式播放流。
// 捕获与播放各跑一个工作线程（MTA），中间用无锁环形缓冲衔接。
class AudioRouterWin : public AudioRouter {
    Q_OBJECT
public:
    explicit AudioRouterWin(QObject* parent = nullptr);
    ~AudioRouterWin() override;

    QVector<DeviceInfo> outputDevices() override;
    bool start(const QString& sourceId, const QString& targetId, float volume) override;
    void stop() override;
    bool isRunning() const override;
    void setVolume(float volume) override;

    // 以下方法仅供内部工作线程/通知回调跨线程调用（勿手动调用）。
    void notifyThreadError(WinSession* session, const QString& message);
    void notifyDevicesChanged();
    void notifyDeviceGone(const QString& id);

private:
    void stopSession(const QString& reason, StopReason stopReason);

    std::shared_ptr<WinSession> m_session;
    NotificationClient* m_notifier = nullptr;
    IMMDeviceEnumerator* m_notificationEnumerator = nullptr;
    bool m_notifierRegistered = false;
};
