#include "audiorouter_linux.h"

#include "realtime_audio.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/latency-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace {

constexpr std::size_t kChannelCount = 2;
constexpr std::size_t kLinkCount = 4;
constexpr int64_t kOperationTimeoutNs = 3LL * SPA_NSEC_PER_SEC;
constexpr const char* kRequestedLatency = "256/48000";
constexpr const char* kRequestedRate = "1/48000";
constexpr const char* kFilterNodeName = "audiomonitor.filter";
constexpr const char* kChannelNames[kChannelCount] = { "FL", "FR" };
constexpr const char* kInputPortNames[kChannelCount] = {
    "monitor_FL",
    "monitor_FR",
};
constexpr const char* kOutputPortNames[kChannelCount] = {
    "playback_FL",
    "playback_FR",
};
constexpr float kMaxVolume = 5.0f;

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "The PipeWire realtime path requires lock-free 32-bit atomics");

uint32_t encodeFloat(float value) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float decodeFloat(uint32_t bits) noexcept
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool propertyIsTrue(const char* value) noexcept
{
    return value && (std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0);
}

QString propertyString(const spa_dict* properties, const char* key)
{
    if (!properties)
        return {};
    const char* value = spa_dict_lookup(properties, key);
    return value ? QString::fromUtf8(value) : QString{};
}

uint32_t propertyId(const spa_dict* properties, const char* key)
{
    bool ok = false;
    const uint32_t result = propertyString(properties, key).toUInt(&ok);
    return ok ? result : SPA_ID_INVALID;
}

QString errorText(int result)
{
    const char* message = spa_strerror(result);
    return message ? QString::fromUtf8(message) : QString::number(result);
}

QString linuxTr(const char* source)
{
    return QCoreApplication::translate("AudioRouterLinux", source);
}

QString defaultSinkNameFromMetadata(const char* type, const char* value)
{
    if (!value)
        return {};

    const QByteArray bytes(value);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject())
        return document.object().value(QStringLiteral("name")).toString();

    // Metadata implementations normally publish a JSON object. Accept a plain
    // Spa:String as a conservative fallback for minimal session managers.
    if (type && std::strcmp(type, "Spa:String") == 0)
        return QString::fromUtf8(value);
    return {};
}

} // namespace

class PipeWireSession {
public:
    explicit PipeWireSession(AudioRouterLinux* owner)
        : m_owner(owner)
    {
        m_volumeBits.store(encodeFloat(1.0f), std::memory_order_relaxed);
        for (LinkHandle& link : m_links)
            link.owner = this;
    }

    ~PipeWireSession()
    {
        shutdown();
    }

    bool initialize(QString* error)
    {
        std::lock_guard<std::mutex> controlGuard(m_controlMutex);
        pw_init(nullptr, nullptr);
        m_pipeWireInitialized = true;

        m_loop = pw_thread_loop_new("audiomonitor-pipewire", nullptr);
        if (!m_loop) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to create the PipeWire thread loop"));
            shutdownLocked();
            return false;
        }

        m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
        if (!m_context) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to create the PipeWire context"));
            shutdownLocked();
            return false;
        }

        const int startResult = pw_thread_loop_start(m_loop);
        if (startResult < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux", "Unable to start the PipeWire thread loop: %1"))
                         .arg(errorText(startResult));
            shutdownLocked();
            return false;
        }
        m_loopStarted = true;

        pw_thread_loop_lock(m_loop);
        m_initializing = true;
        m_asyncError.clear();

        pw_properties* coreProperties = pw_properties_new(
            PW_KEY_APP_NAME, "AudioMonitor",
            PW_KEY_MEDIA_ROLE, "DSP",
            nullptr);
        m_core = pw_context_connect(m_context, coreProperties, 0);
        if (!m_core) {
            m_initializing = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to connect to the PipeWire server"));
            shutdownLocked();
            return false;
        }
        m_coreConnected.store(true, std::memory_order_release);

        pw_core_add_listener(m_core, &m_coreListener, &coreEvents(), this);
        m_coreListenerInstalled = true;

        m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
        if (!m_registry) {
            m_initializing = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to obtain the PipeWire registry"));
            shutdownLocked();
            return false;
        }
        pw_registry_add_listener(m_registry, &m_registryListener, &registryEvents(), this);
        m_registryListenerInstalled = true;

        const bool synchronized = synchronizeLocked(error);
        m_initializing = false;
        if (synchronized)
            m_registryReady = true;
        pw_thread_loop_unlock(m_loop);
        if (!synchronized)
            shutdownLocked();
        return synchronized;
    }

    QVector<DeviceInfo> outputDevices(QString* error)
    {
        std::lock_guard<std::mutex> controlGuard(m_controlMutex);
        if (!m_loop || !m_coreConnected.load(std::memory_order_acquire) || !m_registryReady) {
            if (error)
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux", "PipeWire is not connected"));
            return {};
        }

        pw_thread_loop_lock(m_loop);
        QVector<DeviceInfo> devices;
        devices.reserve(m_nodes.size());
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
            const NodeRecord& node = it.value();
            if (!isUsableSinkLocked(node.id))
                continue;

            DeviceInfo device;
            device.id = stableId(node);
            device.name = node.description.isEmpty() ? node.name : node.description;
            device.isDefault = node.name == m_defaultSinkName
                || (!node.serial.isEmpty() && node.serial == m_defaultSinkName);
            devices.append(device);
        }
        pw_thread_loop_unlock(m_loop);

        std::sort(devices.begin(), devices.end(), [](const DeviceInfo& left, const DeviceInfo& right) {
            if (left.isDefault != right.isDefault)
                return left.isDefault;
            return QString::localeAwareCompare(left.name, right.name) < 0;
        });
        return devices;
    }

    bool start(const QString& sourceId, const QString& targetId, float volume, QString* error)
    {
        std::lock_guard<std::mutex> controlGuard(m_controlMutex);
        if (!m_loop || !m_core || !m_coreConnected.load(std::memory_order_acquire)
            || !m_registryReady) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "PipeWire is not connected"));
            return false;
        }
        if (!std::isfinite(volume)) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "The monitoring volume is not a finite number"));
            return false;
        }

        pw_thread_loop_lock(m_loop);
        ++m_generation;
        m_failureQueued = false;
        cleanupGraphLocked();
        m_starting = true;
        m_asyncError.clear();

        const NodeRecord* source = findSinkLocked(sourceId);
        const NodeRecord* target = findSinkLocked(targetId);
        if (!source || !target) {
            m_starting = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "The selected PipeWire source or target sink is no longer available"));
            return false;
        }
        if (source->id == target->id) {
            m_starting = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "The source and target sinks must be different to prevent feedback"));
            return false;
        }
        // Keep the canonical IDs published by outputDevices().  The caller may
        // still pass a legacy raw name or serial while recovering a session,
        // but reconnect state must use the stable identifier that survives a
        // PipeWire object-id change.
        const SessionDeviceIds canonicalIds{ stableId(*source), stableId(*target) };
        const uint32_t sourceNodeId = source->id;
        const uint32_t targetNodeId = target->id;

        const std::array<uint32_t, kChannelCount> sourcePorts = {
            findPortLocked(sourceNodeId, PW_DIRECTION_OUTPUT, true, QStringLiteral("FL")),
            findPortLocked(sourceNodeId, PW_DIRECTION_OUTPUT, true, QStringLiteral("FR")),
        };
        const std::array<uint32_t, kChannelCount> targetPorts = {
            findPortLocked(targetNodeId, PW_DIRECTION_INPUT, false, QStringLiteral("FL")),
            findPortLocked(targetNodeId, PW_DIRECTION_INPUT, false, QStringLiteral("FR")),
        };
        if (sourcePorts[0] == SPA_ID_INVALID || sourcePorts[1] == SPA_ID_INVALID) {
            m_starting = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "The selected source sink does not expose FL/FR monitor ports"));
            return false;
        }
        if (targetPorts[0] == SPA_ID_INVALID || targetPorts[1] == SPA_ID_INVALID) {
            m_starting = false;
            pw_thread_loop_unlock(m_loop);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "The selected target sink does not expose FL/FR input ports"));
            return false;
        }

        m_selectedSourceNode = sourceNodeId;
        m_selectedTargetNode = targetNodeId;
        m_selectedSourcePorts = sourcePorts;
        m_selectedTargetPorts = targetPorts;
        setVolumeLocked(volume);

        bool ok = createFilterLocked(error);
        if (ok)
            ok = waitForFilterLocked(error);
        if (ok)
            ok = synchronizeLocked(error);

        std::array<uint32_t, kChannelCount> filterInputs{ SPA_ID_INVALID, SPA_ID_INVALID };
        std::array<uint32_t, kChannelCount> filterOutputs{ SPA_ID_INVALID, SPA_ID_INVALID };
        if (ok) {
            m_filterNodeId = pw_filter_get_node_id(m_filter);
            if (m_filterNodeId == SPA_ID_INVALID) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "PipeWire did not publish the monitoring filter node"));
                ok = false;
            }
        }
        if (ok) {
            filterInputs = {
                findNamedPortLocked(m_filterNodeId, PW_DIRECTION_INPUT, kInputPortNames[0]),
                findNamedPortLocked(m_filterNodeId, PW_DIRECTION_INPUT, kInputPortNames[1]),
            };
            filterOutputs = {
                findNamedPortLocked(m_filterNodeId, PW_DIRECTION_OUTPUT, kOutputPortNames[0]),
                findNamedPortLocked(m_filterNodeId, PW_DIRECTION_OUTPUT, kOutputPortNames[1]),
            };
            if (filterInputs[0] == SPA_ID_INVALID || filterInputs[1] == SPA_ID_INVALID
                || filterOutputs[0] == SPA_ID_INVALID || filterOutputs[1] == SPA_ID_INVALID) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "PipeWire did not publish all FL/FR monitoring filter ports"));
                ok = false;
            }
        }

        if (ok) {
            for (std::size_t channel = 0; channel < kChannelCount && ok; ++channel) {
                ok = createLinkLocked(channel,
                                      sourceNodeId,
                                      sourcePorts[channel],
                                      m_filterNodeId,
                                      filterInputs[channel],
                                      error);
                if (ok) {
                    ok = createLinkLocked(kChannelCount + channel,
                                          m_filterNodeId,
                                          filterOutputs[channel],
                                          targetNodeId,
                                          targetPorts[channel],
                                          error);
                }
            }
        }
        if (ok)
            ok = waitForLinksLocked(error);
        if (ok)
            ok = synchronizeLocked(error);
        if (ok && !m_asyncError.isEmpty()) {
            *error = m_asyncError;
            ok = false;
        }

        if (!ok) {
            cleanupGraphLocked();
            m_starting = false;
            pw_thread_loop_unlock(m_loop);
            return false;
        }

        m_starting = false;
        m_running.store(true, std::memory_order_release);
        m_sessionDeviceIds = canonicalIds;
        pw_thread_loop_unlock(m_loop);
        return true;
    }

    bool stop()
    {
        std::lock_guard<std::mutex> controlGuard(m_controlMutex);
        if (!m_loop)
            return false;

        pw_thread_loop_lock(m_loop);
        const bool hadSession = m_filter != nullptr || m_running.load(std::memory_order_relaxed)
            || m_failureQueued;
        ++m_generation;
        m_failureQueued = false;
        cleanupGraphLocked();
        pw_thread_loop_unlock(m_loop);
        return hadSession;
    }

    bool running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

    SessionDeviceIds sessionDeviceIds() const
    {
        return m_sessionDeviceIds;
    }

    void setVolume(float volume) noexcept
    {
        if (!std::isfinite(volume))
            return;
        const float clamped = std::clamp(volume, 0.0f, kMaxVolume);
        m_volumeBits.store(encodeFloat(clamped), std::memory_order_relaxed);
    }

private:
    struct NodeRecord {
        uint32_t id = SPA_ID_INVALID;
        QString serial;
        QString name;
        QString description;
        QString mediaClass;
    };

    struct PortRecord {
        uint32_t id = SPA_ID_INVALID;
        uint32_t nodeId = SPA_ID_INVALID;
        pw_direction direction = PW_DIRECTION_INPUT;
        QString name;
        QString channel;
        bool monitor = false;
    };

    struct FilterPortData {
        uint32_t channel = 0;
    };

    struct LinkHandle {
        PipeWireSession* owner = nullptr;
        pw_proxy* proxy = nullptr;
        spa_hook proxyListener{};
        spa_hook objectListener{};
        pw_link_state state = PW_LINK_STATE_INIT;
        QString error;
        bool listenersInstalled = false;
    };

    static const pw_core_events& coreEvents()
    {
        static const pw_core_events events = [] {
            pw_core_events result{};
            result.version = PW_VERSION_CORE_EVENTS;
            result.done = &PipeWireSession::onCoreDone;
            result.error = &PipeWireSession::onCoreError;
            return result;
        }();
        return events;
    }

    static const pw_registry_events& registryEvents()
    {
        static const pw_registry_events events = [] {
            pw_registry_events result{};
            result.version = PW_VERSION_REGISTRY_EVENTS;
            result.global = &PipeWireSession::onRegistryGlobal;
            result.global_remove = &PipeWireSession::onRegistryGlobalRemove;
            return result;
        }();
        return events;
    }

    static const pw_metadata_events& metadataEvents()
    {
        static const pw_metadata_events events = [] {
            pw_metadata_events result{};
            result.version = PW_VERSION_METADATA_EVENTS;
            result.property = &PipeWireSession::onMetadataProperty;
            return result;
        }();
        return events;
    }

    static const pw_filter_events& filterEvents()
    {
        static const pw_filter_events events = [] {
            pw_filter_events result{};
            result.version = PW_VERSION_FILTER_EVENTS;
            result.state_changed = &PipeWireSession::onFilterStateChanged;
            result.process = &PipeWireSession::onFilterProcess;
            return result;
        }();
        return events;
    }

    static const pw_proxy_events& linkProxyEvents()
    {
        static const pw_proxy_events events = [] {
            pw_proxy_events result{};
            result.version = PW_VERSION_PROXY_EVENTS;
            result.destroy = &PipeWireSession::onLinkProxyDestroy;
            result.removed = &PipeWireSession::onLinkProxyRemoved;
            result.error = &PipeWireSession::onLinkProxyError;
            return result;
        }();
        return events;
    }

    static const pw_link_events& linkEvents()
    {
        static const pw_link_events events = [] {
            pw_link_events result{};
            result.version = PW_VERSION_LINK_EVENTS;
            result.info = &PipeWireSession::onLinkInfo;
            return result;
        }();
        return events;
    }

    static void onCoreDone(void* data, uint32_t id, int sequence)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        if (id == PW_ID_CORE) {
            self->m_lastDoneSequence = sequence;
            pw_thread_loop_signal(self->m_loop, false);
        }
    }

    static void onCoreError(void* data,
                            uint32_t id,
                            int,
                            int result,
                            const char* message)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        const QString detail = message ? QString::fromUtf8(message) : errorText(result);
        const QString error = linuxTr(QT_TRANSLATE_NOOP(
                                  "AudioRouterLinux", "PipeWire core error: %1"))
                                  .arg(detail);

        const bool disconnected = id == PW_ID_CORE && result == -EPIPE;
        if (disconnected)
            self->m_coreConnected.store(false, std::memory_order_release);

        if (self->m_initializing || self->m_starting) {
            if (self->m_asyncError.isEmpty())
                self->m_asyncError = error;
            pw_thread_loop_signal(self->m_loop, false);
            return;
        }
        self->scheduleFailureFromLoop(disconnected ? StopReason::ServiceFailure
                                                   : StopReason::DeviceFailure,
                                      error);
    }

    static void onRegistryGlobal(void* data,
                                 uint32_t id,
                                 uint32_t,
                                 const char* type,
                                 uint32_t version,
                                 const spa_dict* properties)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        if (!properties || !type)
            return;

        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            NodeRecord node;
            node.id = id;
            node.serial = propertyString(properties, PW_KEY_OBJECT_SERIAL);
            node.name = propertyString(properties, PW_KEY_NODE_NAME);
            node.description = propertyString(properties, PW_KEY_NODE_DESCRIPTION);
            node.mediaClass = propertyString(properties, PW_KEY_MEDIA_CLASS);
            if (node.name.isEmpty())
                node.name = QStringLiteral("node-%1").arg(id);
            self->m_nodes.insert(id, node);
            if (self->m_registryReady && isSinkClass(node.mediaClass))
                self->postDevicesChanged();
            return;
        }

        if (std::strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
            const QString direction = propertyString(properties, PW_KEY_PORT_DIRECTION);
            if (direction != QStringLiteral("in") && direction != QStringLiteral("out"))
                return;

            PortRecord port;
            port.id = id;
            port.nodeId = propertyId(properties, PW_KEY_NODE_ID);
            if (port.nodeId == SPA_ID_INVALID)
                return;
            port.direction = direction == QStringLiteral("out") ? PW_DIRECTION_OUTPUT
                                                                 : PW_DIRECTION_INPUT;
            port.name = propertyString(properties, PW_KEY_PORT_NAME);
            port.channel = propertyString(properties, PW_KEY_AUDIO_CHANNEL);
            port.monitor = propertyIsTrue(spa_dict_lookup(properties, PW_KEY_PORT_MONITOR));
            self->m_ports.insert(id, port);

            const auto node = self->m_nodes.constFind(port.nodeId);
            if (self->m_registryReady && node != self->m_nodes.cend()
                && isSinkClass(node->mediaClass)) {
                self->postDevicesChanged();
            }
            return;
        }

        if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0
            && !self->m_metadata
            && propertyString(properties, PW_KEY_METADATA_NAME) == QStringLiteral("default")) {
            self->m_metadata = static_cast<pw_metadata*>(pw_registry_bind(
                self->m_registry,
                id,
                type,
                std::min(version, static_cast<uint32_t>(PW_VERSION_METADATA)),
                0));
            if (self->m_metadata) {
                self->m_metadataId = id;
                const int listenerResult = pw_metadata_add_listener(
                    self->m_metadata, &self->m_metadataListener, &metadataEvents(), self);
                if (listenerResult >= 0) {
                    self->m_metadataListenerInstalled = true;
                } else {
                    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(self->m_metadata));
                    self->m_metadata = nullptr;
                    self->m_metadataId = SPA_ID_INVALID;
                }
            }
        }
    }

    static void onRegistryGlobalRemove(void* data, uint32_t id)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        bool deviceChanged = false;
        bool selectedEndpointRemoved = false;

        const auto port = self->m_ports.constFind(id);
        if (port != self->m_ports.cend()) {
            const auto node = self->m_nodes.constFind(port->nodeId);
            deviceChanged = node != self->m_nodes.cend() && isSinkClass(node->mediaClass);
            selectedEndpointRemoved = self->selectedPortLocked(id);
            self->m_ports.remove(id);
        }

        const auto node = self->m_nodes.constFind(id);
        if (node != self->m_nodes.cend()) {
            deviceChanged = deviceChanged || isSinkClass(node->mediaClass);
            selectedEndpointRemoved = selectedEndpointRemoved || id == self->m_selectedSourceNode
                || id == self->m_selectedTargetNode;
            self->m_nodes.remove(id);
        }

        if (id == self->m_metadataId) {
            if (self->m_metadataListenerInstalled) {
                spa_hook_remove(&self->m_metadataListener);
                self->m_metadataListenerInstalled = false;
            }
            if (self->m_metadata)
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(self->m_metadata));
            self->m_metadata = nullptr;
            self->m_metadataId = SPA_ID_INVALID;
            self->m_defaultSinkName.clear();
            deviceChanged = true;
        }

        if (selectedEndpointRemoved) {
            self->scheduleFailureFromLoop(
                StopReason::DeviceFailure,
                linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "The selected PipeWire source or target sink was removed; monitoring stopped")));
        }
        if (deviceChanged && self->m_registryReady)
            self->postDevicesChanged();
    }

    static int onMetadataProperty(void* data,
                                  uint32_t,
                                  const char* key,
                                  const char* type,
                                  const char* value)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        if (key && std::strcmp(key, "default.audio.sink") != 0)
            return 0;

        const QString newDefault = key ? defaultSinkNameFromMetadata(type, value) : QString{};
        if (newDefault != self->m_defaultSinkName) {
            self->m_defaultSinkName = newDefault;
            if (self->m_registryReady)
                self->postDevicesChanged();
        }
        return 0;
    }

    static void onFilterStateChanged(void* data,
                                     pw_filter_state,
                                     pw_filter_state state,
                                     const char* error)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        self->m_filterState = state;
        if (state == PW_FILTER_STATE_ERROR) {
            const QString detail = error
                ? QString::fromUtf8(error)
                : linuxTr(QT_TRANSLATE_NOOP("AudioRouterLinux", "unknown error"));
            const QString message = linuxTr(QT_TRANSLATE_NOOP(
                                        "AudioRouterLinux", "PipeWire filter error: %1"))
                                        .arg(detail);
            if (self->m_starting) {
                if (self->m_asyncError.isEmpty())
                    self->m_asyncError = message;
            } else {
                self->scheduleFailureFromLoop(StopReason::DeviceFailure, message);
            }
        } else if (state == PW_FILTER_STATE_UNCONNECTED && !self->m_stopping
                   && self->m_running.load(std::memory_order_relaxed)) {
            self->scheduleFailureFromLoop(
                StopReason::DeviceFailure,
                linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "The PipeWire monitoring filter disconnected; monitoring stopped")));
        }
        pw_thread_loop_signal(self->m_loop, false);
    }

    static void onFilterProcess(void* data, spa_io_position* position) noexcept
    {
        auto* self = static_cast<PipeWireSession*>(data);
        if (!position)
            return;

        const uint64_t cycleFrames = position->clock.duration;
        const uint32_t frames = static_cast<uint32_t>(
            std::min<uint64_t>(cycleFrames, std::numeric_limits<uint32_t>::max()));
        if (frames == 0)
            return;

        self->m_graphQuantum.store(frames, std::memory_order_relaxed);
        self->m_graphRateNumerator.store(position->clock.rate.num, std::memory_order_relaxed);
        self->m_graphRateDenominator.store(position->clock.rate.denom, std::memory_order_relaxed);
        self->m_clockFlags.store(position->clock.flags, std::memory_order_relaxed);

        const float* inputLeft = static_cast<const float*>(
            pw_filter_get_dsp_buffer(self->m_inputPorts[0], frames));
        const float* inputRight = static_cast<const float*>(
            pw_filter_get_dsp_buffer(self->m_inputPorts[1], frames));
        float* outputLeft = static_cast<float*>(
            pw_filter_get_dsp_buffer(self->m_outputPorts[0], frames));
        float* outputRight = static_cast<float*>(
            pw_filter_get_dsp_buffer(self->m_outputPorts[1], frames));

        const float volume = decodeFloat(self->m_volumeBits.load(std::memory_order_relaxed));
        realtime_audio::processStereo(
            inputLeft, inputRight, outputLeft, outputRight, frames, volume);
    }

    static void onLinkProxyDestroy(void* data)
    {
        auto* link = static_cast<LinkHandle*>(data);
        if (link->listenersInstalled) {
            spa_hook_remove(&link->objectListener);
            spa_hook_remove(&link->proxyListener);
            link->listenersInstalled = false;
        }
        link->proxy = nullptr;
        link->owner->handleLinkFailureFromLoop(
            link,
            linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "A PipeWire monitoring link was destroyed")));
    }

    static void onLinkProxyRemoved(void* data)
    {
        auto* link = static_cast<LinkHandle*>(data);
        link->owner->handleLinkFailureFromLoop(
            link,
            linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "A PipeWire monitoring link was removed")));
        if (link->proxy)
            pw_proxy_destroy(link->proxy);
    }

    static void onLinkProxyError(void* data, int, int result, const char* message)
    {
        auto* link = static_cast<LinkHandle*>(data);
        const QString detail = message ? QString::fromUtf8(message) : errorText(result);
        link->owner->handleLinkFailureFromLoop(
            link,
            linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "PipeWire could not create a monitoring link: %1"))
                .arg(detail));
    }

    static void onLinkInfo(void* data, const pw_link_info* info)
    {
        auto* link = static_cast<LinkHandle*>(data);
        if (!info)
            return;
        link->state = info->state;
        if (info->state == PW_LINK_STATE_ERROR || info->state == PW_LINK_STATE_UNLINKED) {
            link->owner->handleLinkFailureFromLoop(
                link,
                linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux", "PipeWire monitoring link error: %1"))
                    .arg(info->error ? QString::fromUtf8(info->error)
                                     : QString::fromUtf8(pw_link_state_as_string(info->state))));
        }
        pw_thread_loop_signal(link->owner->m_loop, false);
    }

    static bool isSinkClass(const QString& mediaClass)
    {
        return mediaClass == QStringLiteral("Audio/Sink")
            || mediaClass.startsWith(QStringLiteral("Audio/Sink/"));
    }

    static QString stableId(const NodeRecord& node)
    {
        if (!node.serial.isEmpty())
            return QStringLiteral("serial:%1").arg(node.serial);
        return QStringLiteral("name:%1").arg(node.name);
    }

    const NodeRecord* findSinkLocked(const QString& identifier) const
    {
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
            const NodeRecord& node = it.value();
            if (!isSinkClass(node.mediaClass))
                continue;
            // The raw name/serial fallbacks preserve selections made by older
            // versions while new UI selections use an unambiguous prefix.
            if (stableId(node) == identifier || node.name == identifier || node.serial == identifier)
                return &node;
        }
        return nullptr;
    }

    uint32_t findPortLocked(uint32_t nodeId,
                            pw_direction direction,
                            bool monitor,
                            const QString& channel) const
    {
        for (auto it = m_ports.cbegin(); it != m_ports.cend(); ++it) {
            const PortRecord& port = it.value();
            if (port.nodeId != nodeId || port.direction != direction)
                continue;
            const bool monitorPort = port.monitor || port.name.startsWith(QStringLiteral("monitor_"));
            if (monitor != monitorPort)
                continue;

            QString position = port.channel;
            if (position.isEmpty()) {
                if (port.name.endsWith(QStringLiteral("_FL")))
                    position = QStringLiteral("FL");
                else if (port.name.endsWith(QStringLiteral("_FR")))
                    position = QStringLiteral("FR");
            }
            if (position == channel)
                return port.id;
        }
        return SPA_ID_INVALID;
    }

    uint32_t findNamedPortLocked(uint32_t nodeId,
                                 pw_direction direction,
                                 const char* name) const
    {
        const QString expected = QString::fromLatin1(name);
        for (auto it = m_ports.cbegin(); it != m_ports.cend(); ++it) {
            const PortRecord& port = it.value();
            if (port.nodeId == nodeId && port.direction == direction && port.name == expected)
                return port.id;
        }
        return SPA_ID_INVALID;
    }

    bool isUsableSinkLocked(uint32_t nodeId) const
    {
        return findPortLocked(nodeId, PW_DIRECTION_OUTPUT, true, QStringLiteral("FL"))
                != SPA_ID_INVALID
            && findPortLocked(nodeId, PW_DIRECTION_OUTPUT, true, QStringLiteral("FR"))
                != SPA_ID_INVALID
            && findPortLocked(nodeId, PW_DIRECTION_INPUT, false, QStringLiteral("FL"))
                != SPA_ID_INVALID
            && findPortLocked(nodeId, PW_DIRECTION_INPUT, false, QStringLiteral("FR"))
                != SPA_ID_INVALID;
    }

    bool selectedPortLocked(uint32_t portId) const noexcept
    {
        return std::find(m_selectedSourcePorts.cbegin(), m_selectedSourcePorts.cend(), portId)
                != m_selectedSourcePorts.cend()
            || std::find(m_selectedTargetPorts.cbegin(), m_selectedTargetPorts.cend(), portId)
                != m_selectedTargetPorts.cend();
    }

    bool synchronizeLocked(QString* error)
    {
        if (!m_core || !m_coreConnected.load(std::memory_order_acquire)) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "The PipeWire core disconnected"));
            return false;
        }

        m_asyncError.clear();
        const int sequence = pw_core_sync(m_core, PW_ID_CORE, m_syncSequence);
        if (sequence < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux", "Unable to synchronize with PipeWire: %1"))
                         .arg(errorText(sequence));
            return false;
        }
        m_syncSequence = sequence;

        timespec deadline{};
        const int timeResult = pw_thread_loop_get_time(m_loop, &deadline, kOperationTimeoutNs);
        if (timeResult < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux",
                         "Unable to create a PipeWire synchronization deadline: %1"))
                         .arg(errorText(timeResult));
            return false;
        }

        while (m_lastDoneSequence != sequence && m_asyncError.isEmpty()) {
            const int waitResult = pw_thread_loop_timed_wait_full(m_loop, &deadline);
            if (waitResult == -ETIMEDOUT) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux", "Timed out while synchronizing with PipeWire"));
                return false;
            }
            if (waitResult < 0 && waitResult != -EINTR) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                             "AudioRouterLinux", "PipeWire synchronization failed: %1"))
                             .arg(errorText(waitResult));
                return false;
            }
        }
        if (!m_asyncError.isEmpty()) {
            *error = m_asyncError;
            return false;
        }
        return true;
    }

    bool createFilterLocked(QString* error)
    {
        pw_properties* properties = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_NODE_NAME, kFilterNodeName,
            PW_KEY_NODE_DESCRIPTION, "AudioMonitor native PipeWire filter",
            PW_KEY_NODE_AUTOCONNECT, "false",
            PW_KEY_OBJECT_LINGER, "false",
            PW_KEY_NODE_LATENCY, kRequestedLatency,
            PW_KEY_NODE_RATE, kRequestedRate,
            nullptr);
        if (!properties) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to allocate PipeWire filter properties"));
            return false;
        }

        m_filter = pw_filter_new(m_core, "audiomonitor-filter", properties);
        if (!m_filter) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to create the PipeWire monitoring filter"));
            return false;
        }
        pw_filter_add_listener(m_filter, &m_filterListener, &filterEvents(), this);
        m_filterListenerInstalled = true;
        m_filterState = PW_FILTER_STATE_UNCONNECTED;

        for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
            auto* input = static_cast<FilterPortData*>(pw_filter_add_port(
                m_filter,
                PW_DIRECTION_INPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(FilterPortData),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                    PW_KEY_PORT_NAME, kInputPortNames[channel],
                    PW_KEY_AUDIO_CHANNEL, kChannelNames[channel],
                    PW_KEY_PORT_MONITOR, "false",
                    nullptr),
                nullptr,
                0));
            if (!input) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                             "AudioRouterLinux", "Unable to create PipeWire filter input %1"))
                             .arg(QString::fromLatin1(kChannelNames[channel]));
                return false;
            }
            input->channel = static_cast<uint32_t>(channel);
            m_inputPorts[channel] = input;

            auto* output = static_cast<FilterPortData*>(pw_filter_add_port(
                m_filter,
                PW_DIRECTION_OUTPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(FilterPortData),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                    PW_KEY_PORT_NAME, kOutputPortNames[channel],
                    PW_KEY_AUDIO_CHANNEL, kChannelNames[channel],
                    PW_KEY_PORT_MONITOR, "false",
                    nullptr),
                nullptr,
                0));
            if (!output) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                             "AudioRouterLinux", "Unable to create PipeWire filter output %1"))
                             .arg(QString::fromLatin1(kChannelNames[channel]));
                return false;
            }
            output->channel = static_cast<uint32_t>(channel);
            m_outputPorts[channel] = output;
        }

        uint8_t parameterBuffer[256];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(parameterBuffer, sizeof(parameterBuffer));
        spa_process_latency_info processLatency{};
        processLatency.quantum = 0.0f;
        processLatency.rate = 0;
        processLatency.ns = 0;
        const spa_pod* parameters[] = {
            spa_process_latency_build(&builder, SPA_PARAM_ProcessLatency, &processLatency),
        };

        const int connectResult = pw_filter_connect(
            m_filter, PW_FILTER_FLAG_RT_PROCESS, parameters, SPA_N_ELEMENTS(parameters));
        if (connectResult < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux",
                         "Unable to connect the PipeWire monitoring filter: %1"))
                         .arg(errorText(connectResult));
            return false;
        }
        return true;
    }

    bool waitForFilterLocked(QString* error)
    {
        timespec deadline{};
        const int timeResult = pw_thread_loop_get_time(m_loop, &deadline, kOperationTimeoutNs);
        if (timeResult < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux",
                         "Unable to create a PipeWire filter deadline: %1"))
                         .arg(errorText(timeResult));
            return false;
        }

        while (m_filterState != PW_FILTER_STATE_PAUSED
               && m_filterState != PW_FILTER_STATE_STREAMING
               && m_filterState != PW_FILTER_STATE_ERROR && m_asyncError.isEmpty()) {
            const int waitResult = pw_thread_loop_timed_wait_full(m_loop, &deadline);
            if (waitResult == -ETIMEDOUT) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "Timed out while activating the PipeWire monitoring filter"));
                return false;
            }
            if (waitResult < 0 && waitResult != -EINTR) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                             "AudioRouterLinux", "Waiting for the PipeWire filter failed: %1"))
                             .arg(errorText(waitResult));
                return false;
            }
        }
        if (!m_asyncError.isEmpty()) {
            *error = m_asyncError;
            return false;
        }
        if (m_filterState == PW_FILTER_STATE_ERROR) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "The PipeWire monitoring filter entered an error state"));
            return false;
        }
        return true;
    }

    bool createLinkLocked(std::size_t index,
                          uint32_t outputNode,
                          uint32_t outputPort,
                          uint32_t inputNode,
                          uint32_t inputPort,
                          QString* error)
    {
        LinkHandle& link = m_links[index];
        link.state = PW_LINK_STATE_INIT;
        link.error.clear();

        pw_properties* properties = pw_properties_new(nullptr, nullptr);
        if (!properties) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to allocate PipeWire link properties"));
            return false;
        }
        const int propertyResults[] = {
            pw_properties_setf(properties, PW_KEY_LINK_OUTPUT_NODE, "%u", outputNode),
            pw_properties_setf(properties, PW_KEY_LINK_OUTPUT_PORT, "%u", outputPort),
            pw_properties_setf(properties, PW_KEY_LINK_INPUT_NODE, "%u", inputNode),
            pw_properties_setf(properties, PW_KEY_LINK_INPUT_PORT, "%u", inputPort),
            pw_properties_set(properties, PW_KEY_OBJECT_LINGER, "false"),
            pw_properties_set(properties, PW_KEY_LINK_PASSIVE, "false"),
        };
        bool propertyFailure = false;
        for (const int result : propertyResults)
            propertyFailure = propertyFailure || result < 0;
        if (propertyFailure) {
            pw_properties_free(properties);
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux",
                "Unable to populate PipeWire monitoring link properties"));
            return false;
        }

        link.proxy = static_cast<pw_proxy*>(pw_core_create_object(
            m_core,
            "link-factory",
            PW_TYPE_INTERFACE_Link,
            PW_VERSION_LINK,
            &properties->dict,
            0));
        pw_properties_free(properties);
        if (!link.proxy) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                "AudioRouterLinux", "Unable to create a PipeWire monitoring link"));
            return false;
        }

        pw_proxy_add_listener(link.proxy, &link.proxyListener, &linkProxyEvents(), &link);
        pw_proxy_add_object_listener(link.proxy, &link.objectListener, &linkEvents(), &link);
        link.listenersInstalled = true;
        return true;
    }

    bool waitForLinksLocked(QString* error)
    {
        timespec deadline{};
        const int timeResult = pw_thread_loop_get_time(m_loop, &deadline, kOperationTimeoutNs);
        if (timeResult < 0) {
            *error = linuxTr(QT_TRANSLATE_NOOP(
                         "AudioRouterLinux", "Unable to create a PipeWire link deadline: %1"))
                         .arg(errorText(timeResult));
            return false;
        }

        while (!linksDefinitiveLocked() && m_asyncError.isEmpty()) {
            const int waitResult = pw_thread_loop_timed_wait_full(m_loop, &deadline);
            if (waitResult == -ETIMEDOUT) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                    "AudioRouterLinux",
                    "Timed out while negotiating PipeWire monitoring links"));
                return false;
            }
            if (waitResult < 0 && waitResult != -EINTR) {
                *error = linuxTr(QT_TRANSLATE_NOOP(
                             "AudioRouterLinux",
                             "Waiting for PipeWire monitoring links failed: %1"))
                             .arg(errorText(waitResult));
                return false;
            }
        }
        if (!m_asyncError.isEmpty()) {
            *error = m_asyncError;
            return false;
        }
        for (const LinkHandle& link : m_links) {
            if (link.state != PW_LINK_STATE_PAUSED && link.state != PW_LINK_STATE_ACTIVE) {
                *error = link.error.isEmpty()
                    ? linuxTr(QT_TRANSLATE_NOOP(
                          "AudioRouterLinux",
                          "A PipeWire monitoring link did not become active"))
                    : link.error;
                return false;
            }
        }
        return true;
    }

    bool linksDefinitiveLocked() const noexcept
    {
        for (const LinkHandle& link : m_links) {
            if (link.state != PW_LINK_STATE_PAUSED && link.state != PW_LINK_STATE_ACTIVE
                && link.state != PW_LINK_STATE_ERROR && link.state != PW_LINK_STATE_UNLINKED) {
                return false;
            }
        }
        return true;
    }

    void handleLinkFailureFromLoop(LinkHandle* link, const QString& message)
    {
        link->state = PW_LINK_STATE_ERROR;
        link->error = message;
        if (m_starting) {
            if (m_asyncError.isEmpty())
                m_asyncError = message;
        } else {
            scheduleFailureFromLoop(StopReason::DeviceFailure, message);
        }
        pw_thread_loop_signal(m_loop, false);
    }

    void setVolumeLocked(float volume) noexcept
    {
        const float clamped = std::clamp(volume, 0.0f, kMaxVolume);
        m_volumeBits.store(encodeFloat(clamped), std::memory_order_relaxed);
    }

    void cleanupGraphLocked()
    {
        m_stopping = true;
        m_running.store(false, std::memory_order_release);

        for (LinkHandle& link : m_links) {
            if (link.proxy) {
                if (link.listenersInstalled) {
                    spa_hook_remove(&link.objectListener);
                    spa_hook_remove(&link.proxyListener);
                    link.listenersInstalled = false;
                }
                pw_proxy_destroy(link.proxy);
                link.proxy = nullptr;
            }
            link.state = PW_LINK_STATE_INIT;
            link.error.clear();
        }

        if (m_filter) {
            if (m_filterListenerInstalled) {
                spa_hook_remove(&m_filterListener);
                m_filterListenerInstalled = false;
            }
            pw_filter_disconnect(m_filter);
            pw_filter_destroy(m_filter);
            m_filter = nullptr;
        }
        m_inputPorts.fill(nullptr);
        m_outputPorts.fill(nullptr);
        m_filterNodeId = SPA_ID_INVALID;
        m_filterState = PW_FILTER_STATE_UNCONNECTED;
        m_selectedSourceNode = SPA_ID_INVALID;
        m_selectedTargetNode = SPA_ID_INVALID;
        m_selectedSourcePorts.fill(SPA_ID_INVALID);
        m_selectedTargetPorts.fill(SPA_ID_INVALID);
        m_starting = false;
        m_stopping = false;
    }

    void scheduleFailureFromLoop(StopReason reason, const QString& message)
    {
        if (m_shuttingDown || m_stopping)
            return;
        if (m_starting) {
            if (m_asyncError.isEmpty())
                m_asyncError = message;
            pw_thread_loop_signal(m_loop, false);
            return;
        }
        if (!m_running.exchange(false, std::memory_order_acq_rel) || m_failureQueued)
            return;

        m_failureQueued = true;
        const uint64_t generation = m_generation;
        QPointer<AudioRouterLinux> owner = m_owner;
        PipeWireSession* session = this;
        QMetaObject::invokeMethod(
            m_owner,
            [owner, session, generation, reason, message]() {
                if (owner)
                    session->deliverFailure(generation, reason, message);
            },
            Qt::QueuedConnection);
    }

    void deliverFailure(uint64_t generation, StopReason reason, const QString& message)
    {
        {
            std::lock_guard<std::mutex> controlGuard(m_controlMutex);
            if (!m_loop)
                return;
            pw_thread_loop_lock(m_loop);
            if (generation != m_generation || !m_failureQueued) {
                pw_thread_loop_unlock(m_loop);
                return;
            }
            cleanupGraphLocked();
            m_failureQueued = false;
            ++m_generation;
            pw_thread_loop_unlock(m_loop);
        }

        m_owner->notifyError(message);
        m_owner->notifyStopped(reason);
    }

    void postDevicesChanged()
    {
        QPointer<AudioRouterLinux> owner = m_owner;
        QMetaObject::invokeMethod(
            m_owner,
            [owner]() {
                if (owner)
                    owner->notifyDevicesChanged();
            },
            Qt::QueuedConnection);
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> controlGuard(m_controlMutex);
        shutdownLocked();
    }

    // Called with m_controlMutex held (including during initialization).
    void shutdownLocked()
    {
        if (m_loop && m_loopStarted) {
            pw_thread_loop_lock(m_loop);
            m_shuttingDown = true;
            ++m_generation;
            m_failureQueued = false;
            cleanupGraphLocked();

            if (m_metadataListenerInstalled) {
                spa_hook_remove(&m_metadataListener);
                m_metadataListenerInstalled = false;
            }
            if (m_metadata) {
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_metadata));
                m_metadata = nullptr;
            }
            m_metadataId = SPA_ID_INVALID;

            if (m_registryListenerInstalled) {
                spa_hook_remove(&m_registryListener);
                m_registryListenerInstalled = false;
            }
            if (m_registry) {
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_registry));
                m_registry = nullptr;
            }

            if (m_coreListenerInstalled) {
                spa_hook_remove(&m_coreListener);
                m_coreListenerInstalled = false;
            }
            if (m_core) {
                pw_core_disconnect(m_core);
                m_core = nullptr;
            }
            m_coreConnected.store(false, std::memory_order_release);
            pw_thread_loop_unlock(m_loop);
        }

        if (m_loopStarted) {
            pw_thread_loop_stop(m_loop);
            m_loopStarted = false;
        }
        if (m_context) {
            pw_context_destroy(m_context);
            m_context = nullptr;
        }
        if (m_loop) {
            pw_thread_loop_destroy(m_loop);
            m_loop = nullptr;
        }
        if (m_pipeWireInitialized) {
            pw_deinit();
            m_pipeWireInitialized = false;
        }
    }

    AudioRouterLinux* m_owner = nullptr;
    mutable std::mutex m_controlMutex;

    pw_thread_loop* m_loop = nullptr;
    pw_context* m_context = nullptr;
    pw_core* m_core = nullptr;
    pw_registry* m_registry = nullptr;
    pw_metadata* m_metadata = nullptr;
    pw_filter* m_filter = nullptr;

    spa_hook m_coreListener{};
    spa_hook m_registryListener{};
    spa_hook m_metadataListener{};
    spa_hook m_filterListener{};
    bool m_coreListenerInstalled = false;
    bool m_registryListenerInstalled = false;
    bool m_metadataListenerInstalled = false;
    bool m_filterListenerInstalled = false;

    std::array<void*, kChannelCount> m_inputPorts{};
    std::array<void*, kChannelCount> m_outputPorts{};
    std::array<LinkHandle, kLinkCount> m_links{};

    QHash<uint32_t, NodeRecord> m_nodes;
    QHash<uint32_t, PortRecord> m_ports;
    QString m_defaultSinkName;
    QString m_asyncError;
    SessionDeviceIds m_sessionDeviceIds;

    int m_syncSequence = 0;
    int m_lastDoneSequence = -1;
    uint32_t m_metadataId = SPA_ID_INVALID;
    uint32_t m_filterNodeId = SPA_ID_INVALID;
    pw_filter_state m_filterState = PW_FILTER_STATE_UNCONNECTED;

    uint32_t m_selectedSourceNode = SPA_ID_INVALID;
    uint32_t m_selectedTargetNode = SPA_ID_INVALID;
    std::array<uint32_t, kChannelCount> m_selectedSourcePorts{ SPA_ID_INVALID, SPA_ID_INVALID };
    std::array<uint32_t, kChannelCount> m_selectedTargetPorts{ SPA_ID_INVALID, SPA_ID_INVALID };

    std::atomic<bool> m_running{ false };
    std::atomic<uint32_t> m_volumeBits{};
    std::atomic<uint32_t> m_graphQuantum{ 0 };
    std::atomic<uint32_t> m_graphRateNumerator{ 0 };
    std::atomic<uint32_t> m_graphRateDenominator{ 0 };
    std::atomic<uint32_t> m_clockFlags{ 0 };

    uint64_t m_generation = 0;
    bool m_pipeWireInitialized = false;
    bool m_loopStarted = false;
    std::atomic<bool> m_coreConnected{ false };
    bool m_registryReady = false;
    bool m_initializing = false;
    bool m_starting = false;
    bool m_stopping = false;
    bool m_shuttingDown = false;
    bool m_failureQueued = false;
};

AudioRouterLinux::AudioRouterLinux(QObject* parent)
    : AudioRouter(parent)
    , m_session(std::make_unique<PipeWireSession>(this))
{
    m_session->initialize(&m_initializationError);
}

AudioRouterLinux::~AudioRouterLinux() = default;

void AudioRouterLinux::reinitializeSession()
{
    // This runs from a queued GUI callback, after the failure-delivery method
    // on the previous session has returned. Destroying the session earlier
    // would invalidate the object whose callback is still unwinding.
    m_reinitializationQueued = false;
    m_session.reset();
    m_initializationError.clear();
    m_session = std::make_unique<PipeWireSession>(this);
    if (!m_session->initialize(&m_initializationError) && !m_initializationError.isEmpty())
        emit errorOccurred(m_initializationError);
}

QVector<DeviceInfo> AudioRouterLinux::outputDevices()
{
    if (!m_initializationError.isEmpty()) {
        // A service may have been unavailable only briefly. Retry creating the
        // session on the next enumeration so automatic recovery can proceed.
        reinitializeSession();
        if (!m_initializationError.isEmpty()) {
            emit errorOccurred(m_initializationError);
            return {};
        }
    }

    QString error;
    QVector<DeviceInfo> devices = m_session->outputDevices(&error);
    if (!error.isEmpty())
        emit errorOccurred(error);
    return devices;
}

bool AudioRouterLinux::start(const QString& sourceId, const QString& targetId, float volume)
{
    // A new attempt supersedes any previous recovery snapshot.  The snapshot
    // is republished only after the PipeWire graph is fully running.
    m_lastSessionDeviceIds = {};
    if (m_captureDumpRequested || m_playbackDumpRequested || m_callbackDumpRequested) {
        emit errorOccurred(linuxTr(QT_TRANSLATE_NOOP(
            "AudioRouterLinux",
            "Debug audio dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe")));
        return false;
    }
    if (!m_initializationError.isEmpty()) {
        emit errorOccurred(m_initializationError);
        return false;
    }

    QString error;
    if (!m_session->start(sourceId, targetId, volume, &error)) {
        emit errorOccurred(error);
        return false;
    }
    m_lastSessionDeviceIds = m_session->sessionDeviceIds();
    emit started();
    return true;
}

void AudioRouterLinux::stop()
{
    const bool sessionStopped = m_session && m_session->stop();
    m_lastSessionDeviceIds = {};
    if (sessionStopped)
        emit stopped(StopReason::UserRequested);
}

bool AudioRouterLinux::isRunning() const
{
    return m_session && m_session->running();
}

SessionDeviceIds AudioRouterLinux::lastSessionDeviceIds() const
{
    return m_lastSessionDeviceIds;
}

void AudioRouterLinux::setVolume(float volume)
{
    if (m_session)
        m_session->setVolume(volume);
}

void AudioRouterLinux::setCaptureDumpFile(const QString& path)
{
    m_captureDumpRequested = !path.isEmpty();
    if (m_captureDumpRequested && isRunning()) {
        emit errorOccurred(linuxTr(QT_TRANSLATE_NOOP(
            "AudioRouterLinux",
            "Capture dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe")));
    }
}

void AudioRouterLinux::setPlaybackDumpFile(const QString& path)
{
    m_playbackDumpRequested = !path.isEmpty();
    if (m_playbackDumpRequested && isRunning()) {
        emit errorOccurred(linuxTr(QT_TRANSLATE_NOOP(
            "AudioRouterLinux",
            "Playback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe")));
    }
}

void AudioRouterLinux::setCallbackDumpFile(const QString& path)
{
    m_callbackDumpRequested = !path.isEmpty();
    if (m_callbackDumpRequested && isRunning()) {
        emit errorOccurred(linuxTr(QT_TRANSLATE_NOOP(
            "AudioRouterLinux",
            "Callback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe")));
    }
}

void AudioRouterLinux::notifyError(const QString& message)
{
    emit errorOccurred(message);
}

void AudioRouterLinux::notifyStopped(StopReason reason)
{
    if (reason == StopReason::ServiceFailure && !m_reinitializationQueued) {
        m_reinitializationQueued = true;
        QPointer<AudioRouterLinux> owner(this);
        QMetaObject::invokeMethod(
            this,
            [owner]() {
                if (owner)
                    owner->reinitializeSession();
            },
            Qt::QueuedConnection);
    }
    emit stopped(reason);
}

void AudioRouterLinux::notifyDevicesChanged()
{
    emit deviceListChanged();
}
