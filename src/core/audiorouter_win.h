#pragma once

#include "audiorouter.h"

#include <memory>

class WinSession;
class NotificationClient;
struct IMMDeviceEnumerator;

// Windows implementation:
//  - The selected source output is captured with WASAPI loopback
//    (AUDCLNT_STREAMFLAGS_LOOPBACK) to obtain the audio currently playing.
//  - The target output receives a shared-mode WASAPI render stream.
// Capture and render each run on their own MTA worker and exchange frames via
// a bounded lock-free ring buffer.
class AudioRouterWin : public AudioRouter {
    Q_OBJECT
public:
    explicit AudioRouterWin(QObject* parent = nullptr);
    ~AudioRouterWin() override;

    QVector<DeviceInfo> outputDevices() override;
    bool start(const QString& sourceId, const QString& targetId, float volume) override;
    void stop() override;
    bool isRunning() const override;
    SessionDeviceIds lastSessionDeviceIds() const override;
    void setVolume(float volume) override;

    // Internal entry points used by worker threads and notification callbacks.
    // They must not be called by application code.
    void notifyThreadError(WinSession* session, const QString& message);
    void notifyDevicesChanged();
    void notifyDeviceGone(const QString& id);

private:
    void stopSession(const QString& reason, StopReason stopReason);

    std::shared_ptr<WinSession> m_session;
    SessionDeviceIds m_lastSessionDeviceIds;
    NotificationClient* m_notifier = nullptr;
    IMMDeviceEnumerator* m_notificationEnumerator = nullptr;
    bool m_notifierRegistered = false;
};
