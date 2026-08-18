#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// 音频输出设备信息。id 为平台相关的稳定标识：
//  - Windows: WASAPI 端点 ID
//  - Linux:   PipeWire object.serial（不可用时回退到 node.name）
struct DeviceInfo {
    QString id;
    QString name;
    bool isDefault = false;
};

// 转发停止的原因，用于判断是否值得自动重连。
enum class StopReason {
    UserRequested, // 用户主动停止 / 程序退出
    DeviceFailure, // 设备被移除、断开或流异常终止
    ServiceFailure // 音频服务（PipeWire、Windows Audio）不可用
};
Q_DECLARE_METATYPE(StopReason)

// 音频监听转发引擎的抽象接口。
//
// 语义与 OBS 的"音频监听"一致：
//  监听源 = 一个【输出设备】（抓取其正在播放的音频，即回环/监听端口）；
//  转发目标 = 另一个【输出设备】（把抓到的音频播放过去）。
//
// start() 之前应先在 GUI 线程调用 outputDevices() 获取可选设备。
class AudioRouter : public QObject {
    Q_OBJECT
public:
    explicit AudioRouter(QObject* parent = nullptr) : QObject(parent) {}
    static AudioRouter* create(QObject* parent = nullptr);
    ~AudioRouter() override = default;

    // 枚举可作为监听源/转发目标的输出设备（GUI 线程调用，内部会等待枚举完成）。
    virtual QVector<DeviceInfo> outputDevices() = 0;

    // 开始转发。volume 范围 0.0 ~ 2.0（1.0 = 原始音量）。
    // 成功返回 true 并发出 started()；失败返回 false 并发出 errorOccurred()。
    virtual bool start(const QString& sourceId, const QString& targetId, float volume) = 0;

    // 停止转发（未运行时为空操作）。停止后发出 stopped()。
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;

    // 运行中实时调节音量，范围 0.0 ~ 2.0。
    virtual void setVolume(float volume) = 0;

    // 调试导出是后端可选能力；不支持的后端会通过 errorOccurred() 明确拒绝。
    // 把捕获到的原始 float32 交错音频写入指定文件（空路径=禁用）。
    virtual void setCaptureDumpFile(const QString& path) { Q_UNUSED(path); }
    // 调试用：把写入播放流的原始数据导出到文件。
    virtual void setPlaybackDumpFile(const QString& path) { Q_UNUSED(path); }
    // 调试用：记录每次播放回调的字节数。
    virtual void setCallbackDumpFile(const QString& path) { Q_UNUSED(path); }

signals:
    void started();
    // reason 说明本次停止是否由设备/服务故障引起，供上层决定是否自动重连。
    void stopped(StopReason reason = StopReason::UserRequested);
    void errorOccurred(const QString& message);
    void deviceListChanged();   // 设备热插拔/默认设备变化
};
