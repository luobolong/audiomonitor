#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// Output device information. The ID is a platform-specific stable identifier:
//  - Windows: WASAPI endpoint ID
//  - Linux:   PipeWire object.serial, falling back to node.name when absent
struct DeviceInfo {
    QString id;
    QString name;
    bool isDefault = false;
};

// Device IDs captured from the most recently started audio session.  Backends
// keep this snapshot independent of any GUI device-list model so a refresh
// cannot change the IDs used for automatic recovery.
struct SessionDeviceIds {
    QString sourceId;
    QString targetId;
};

// Why forwarding stopped, used to decide whether automatic recovery is useful.
enum class StopReason {
    UserRequested, // Explicit stop or application shutdown.
    DeviceFailure, // Device removal, disconnect, or stream failure.
    ServiceFailure // PipeWire or Windows Audio is unavailable.
};
Q_DECLARE_METATYPE(StopReason)

// Abstract audio monitoring and forwarding engine.
//
// The source is an output device whose currently playing audio is captured
// through a loopback/monitor path. The target is a different output device
// that receives the captured audio.
//
// Call outputDevices() from the GUI thread before start().
class AudioRouter : public QObject {
    Q_OBJECT
public:
    explicit AudioRouter(QObject* parent = nullptr) : QObject(parent) {}
    static AudioRouter* create(QObject* parent = nullptr);
    ~AudioRouter() override = default;

    // Enumerate output devices usable as source or target (GUI thread only).
    virtual QVector<DeviceInfo> outputDevices() = 0;

    // Start forwarding. volume is in the range 0.0 to 5.0 (1.0 is unity).
    // On success emits started(); on failure returns false and emits an error.
    virtual bool start(const QString& sourceId, const QString& targetId, float volume) = 0;

    // Stop forwarding. Stopping an idle router is a no-op.
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;

    // Return the IDs used by the current session, or by the session that most
    // recently stopped because of a device/service failure.  The default is
    // empty for backends that do not expose session recovery state.
    virtual SessionDeviceIds lastSessionDeviceIds() const { return {}; }

    // Change the volume while running, in the range 0.0 to 5.0.
    virtual void setVolume(float volume) = 0;

    // Debug dumps are optional backend capabilities. Unsupported backends
    // reject them through errorOccurred(). Capture output is interleaved
    // float32 audio; an empty path disables the dump.
    virtual void setCaptureDumpFile(const QString& path) { Q_UNUSED(path); }
    // Debug-only dump of raw data submitted to the render stream.
    virtual void setPlaybackDumpFile(const QString& path) { Q_UNUSED(path); }
    // Debug-only log of the byte count submitted by each render callback.
    virtual void setCallbackDumpFile(const QString& path) { Q_UNUSED(path); }

signals:
    void started();
    // reason lets the application distinguish faults from an explicit stop.
    void stopped(StopReason reason = StopReason::UserRequested);
    void errorOccurred(const QString& message);
    void deviceListChanged();   // Device hotplug or default-device change.
};
