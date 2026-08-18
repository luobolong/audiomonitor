#pragma once

#include "audiorouter.h"

#include <memory>

class PipeWireSession;

// Native PipeWire implementation. One pw_filter owns planar FL/FR input and
// output DSP ports. Explicit PipeWire links insert it between the selected
// sink's monitor ports and the target sink; there is no application PCM FIFO.
class AudioRouterLinux : public AudioRouter {
    Q_OBJECT
public:
    explicit AudioRouterLinux(QObject* parent = nullptr);
    ~AudioRouterLinux() override;

    QVector<DeviceInfo> outputDevices() override;
    bool start(const QString& sourceId, const QString& targetId, float volume) override;
    void stop() override;
    bool isRunning() const override;
    void setVolume(float volume) override;
    void setCaptureDumpFile(const QString& path) override;
    void setPlaybackDumpFile(const QString& path) override;
    void setCallbackDumpFile(const QString& path) override;

    // 以下方法仅供内部工作线程跨线程调用（勿手动调用）。
    void notifyError(const QString& message);
    void notifyStopped(StopReason reason);
    void notifyDevicesChanged();

private:
    std::unique_ptr<PipeWireSession> m_session;
    QString m_initializationError;
    bool m_captureDumpRequested = false;
    bool m_playbackDumpRequested = false;
    bool m_callbackDumpRequested = false;
};
