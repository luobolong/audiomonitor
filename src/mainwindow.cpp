#include "mainwindow.h"

#include "appicon.h"
#include "core/audiorouter.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QLocale>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSlider>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>
#include <QWheelEvent>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace {
// Reconnect backoff sequence (milliseconds). Device re-enumeration normally
// completes within a few seconds.
constexpr int kReconnectDelaysMs[] = {1000, 2000, 4000, 8000, 15000};
constexpr int kMaxReconnectAttempts = int(std::size(kReconnectDelaysMs));

constexpr auto kLanguageSetting = "ui/language";
constexpr auto kEnglishLanguage = "en";
constexpr auto kSimplifiedChineseLanguage = "zh_CN";
constexpr int kDeviceNameRole = Qt::UserRole + 1;
constexpr int kDeviceDefaultRole = Qt::UserRole + 2;
constexpr int kVolumeMinPercent = 0;
constexpr int kVolumeMaxPercent = 500;
constexpr int kVolumeUniformMaxPercent = 200;
constexpr int kVolumeHighRangePercent = kVolumeMaxPercent - kVolumeUniformMaxPercent;
constexpr int kVolumeUniformTrackMax = kVolumeMaxPercent / 2;
constexpr int kVolumeHighTrackRange = kVolumeMaxPercent - kVolumeUniformTrackMax;
constexpr int kVolumeDefaultPercent = 100;
constexpr float kVolumePercentScale = 100.0f;
constexpr int kVolumeSnapThreshold = 10;
constexpr std::array<int, 8> kVolumeTickValues = {
    0, 50, 100, 150, 200, 300, 400, 500
};

int snappedVolumeValue(int value) noexcept
{
    int closest = value;
    int closestDistance = kVolumeSnapThreshold + 1;
    for (const int snapValue : kVolumeTickValues) {
        const int distance = std::abs(value - snapValue);
        if (distance <= kVolumeSnapThreshold && distance < closestDistance) {
            closest = snapValue;
            closestDistance = distance;
        }
    }
    return closest;
}

class VolumeSlider final : public QSlider {
public:
    using QSlider::QSlider;

    void paintEvent(QPaintEvent* event) override
    {
        QSlider::paintEvent(event);

        QStyleOptionSlider option;
        initStyleOption(&option);
        QPainter painter(this);
        const QPalette::ColorGroup colorGroup = isEnabled() ? QPalette::Active
                                                              : QPalette::Disabled;
        const QColor textColor = palette().color(colorGroup, QPalette::WindowText);
        painter.setPen(QPen(textColor, 1));
        painter.setFont(font());
        const QFontMetrics metrics(painter.font());

        const int tickY = std::max(0, height() - metrics.height() - 8);
        const int labelTop = tickY + 8;
        struct TickLabel {
            QRect rect;
            QString text;
        };
        std::vector<TickLabel> labels;
        for (const int volume : kVolumeTickValues) {
            option.sliderPosition = positionForVolume(volume);
            option.sliderValue = option.sliderPosition;
            const QRect handle = style()->subControlRect(
                QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
            if (!handle.isValid())
                continue;
            painter.drawLine(handle.center().x(), tickY, handle.center().x(),
                             std::min(height() - 1, tickY + 6));

            const double gain = double(volume) / double(kVolumePercentScale);
            const QString label = QStringLiteral("%1x").arg(gain, 0, 'g', 3);
            const int labelWidth = metrics.horizontalAdvance(label);
            const int labelX = std::clamp(handle.center().x() - labelWidth / 2,
                                          0, std::max(0, width() - labelWidth));
            labels.push_back({ QRect(labelX, labelTop, labelWidth, metrics.height()), label });
        }

        // Keep every tick visible, but omit labels that would overlap when
        // the window is narrow. The endpoints remain visible at all widths.
        constexpr int kLabelGap = 4;
        std::vector<TickLabel> visibleLabels;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const bool isEndpoint = i == 0 || i + 1 == labels.size();
            if (!isEndpoint && !visibleLabels.empty()
                && labels[i].rect.left() <= visibleLabels.back().rect.right() + kLabelGap) {
                continue;
            }
            if (isEndpoint && i + 1 == labels.size()) {
                while (!visibleLabels.empty()
                       && labels[i].rect.left()
                           <= visibleLabels.back().rect.right() + kLabelGap) {
                    visibleLabels.pop_back();
                }
            }
            visibleLabels.push_back(labels[i]);
        }
        for (const TickLabel& label : visibleLabels) {
            painter.drawText(label.rect, Qt::AlignHCenter | Qt::AlignTop, label.text);
        }
    }

    // Keep the original uniform 0-200 range, then use a square curve that
    // packs larger values into the high end of the slider.
    static int volumeForPosition(int position) noexcept
    {
        const int clamped = std::clamp(position, kVolumeMinPercent, kVolumeMaxPercent);
        if (clamped <= kVolumeUniformTrackMax) {
            const double normalized = double(clamped) / double(kVolumeUniformTrackMax);
            return int(std::lround(normalized * double(kVolumeUniformMaxPercent)));
        }
        const double normalized = double(clamped - kVolumeUniformTrackMax)
            / double(kVolumeHighTrackRange);
        return kVolumeUniformMaxPercent
            + int(std::lround(normalized * normalized * double(kVolumeHighRangePercent)));
    }

    static int positionForVolume(int volume) noexcept
    {
        const int clamped = std::clamp(volume, kVolumeMinPercent, kVolumeMaxPercent);
        if (clamped <= kVolumeUniformMaxPercent) {
            const double normalized = double(clamped) / double(kVolumeUniformMaxPercent);
            return int(std::lround(normalized * double(kVolumeUniformTrackMax)));
        }
        const double normalized = double(clamped - kVolumeUniformMaxPercent)
            / double(kVolumeHighRangePercent);
        return kVolumeUniformTrackMax
            + int(std::lround(std::sqrt(normalized) * double(kVolumeHighTrackRange)));
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        QSlider::mouseMoveEvent(event);
        snapToSpecialValue();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QSlider::mouseReleaseEvent(event);
        snapToSpecialValue();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        QSlider::wheelEvent(event);
        snapToSpecialValue();
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        QSlider::keyReleaseEvent(event);
        snapToSpecialValue();
    }

private:
    void snapToSpecialValue()
    {
        const int volume = volumeForPosition(value());
        const int snapped = snappedVolumeValue(volume);
        if (snapped != volume)
            setValue(positionForVolume(snapped));
    }
};

QString colorStatus(const QString& color, const QString& text)
{
    return QStringLiteral("<span style='color:%1;'>%2</span>").arg(color, text);
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(nullptr, parent)
{
}

MainWindow::MainWindow(AudioRouter* router, QWidget* parent)
    : QMainWindow(parent)
    , m_router(router)
{
    setWindowIcon(makeAppIcon());

    m_languageCode = configuredLanguage();
    m_translator = new QTranslator(this);
    // Install the catalog before creating widgets so their initial text is translated.
    if (m_languageCode == QLatin1String(kSimplifiedChineseLanguage)) {
        const QString path = QStringLiteral(":/translations/audiomonitor_%1.qm")
                                  .arg(m_languageCode);
        if (m_translator->load(path))
            qApp->installTranslator(m_translator);
        else
            m_languageCode = QLatin1String(kEnglishLanguage);
    }

    // Backend construction can produce human-readable initialization errors,
    // so create the production router only after the selected catalog is active.
    if (!m_router)
        m_router = AudioRouter::create(this);
    else if (!m_router->parent())
        m_router->setParent(this);

    buildUi();

    connect(m_router, &AudioRouter::started, this, &MainWindow::onStarted);
    connect(m_router, &AudioRouter::stopped, this, &MainWindow::onStopped);
    connect(m_router, &AudioRouter::errorOccurred, this, &MainWindow::onError);
    connect(m_router, &AudioRouter::deviceListChanged, this, &MainWindow::refreshDevices);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::attemptReconnect);

    loadSettings();
    refreshDevices();

    // Apply persisted selections after the first device refresh.
    applySavedSelection(m_source, m_savedSourceId, m_savedSourceName);
    applySavedSelection(m_target, m_savedTargetId, m_savedTargetName);
    m_savedSourceId.clear();
    m_savedSourceName.clear();
    m_savedTargetId.clear();
    m_savedTargetName.clear();

    updateUiState();
}

MainWindow::~MainWindow()
{
    if (m_translator)
        qApp->removeTranslator(m_translator);
    saveSettings();
    if (m_router)
        m_router->stop();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_source = new QComboBox(central);
    m_source->setObjectName(QStringLiteral("sourceDeviceCombo"));
    m_source->setMinimumWidth(320);
    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(m_source, 1);
    m_refresh = new QPushButton(central);
    sourceRow->addWidget(m_refresh);
    m_sourceTitle = new QLabel(central);
    form->addRow(m_sourceTitle, sourceRow);

    m_target = new QComboBox(central);
    m_target->setObjectName(QStringLiteral("targetDeviceCombo"));
    m_target->setMinimumWidth(320);
    m_targetTitle = new QLabel(central);
    form->addRow(m_targetTitle, m_target);

    m_hint = new QLabel(central);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #666; font-size: 12px;"));
    form->addRow(QString(), m_hint);

    m_volume = new VolumeSlider(Qt::Horizontal, central);
    m_volume->setObjectName(QStringLiteral("volumeSlider"));
    m_volume->setRange(kVolumeMinPercent, kVolumeMaxPercent);
    m_volume->setValue(VolumeSlider::positionForVolume(kVolumeDefaultPercent));
    m_volume->setTickPosition(QSlider::NoTicks);
    m_volume->setMinimumHeight(42);
    m_volumeInput = new QSpinBox(central);
    m_volumeInput->setObjectName(QStringLiteral("volumeInput"));
    m_volumeInput->setRange(kVolumeMinPercent, kVolumeMaxPercent);
    m_volumeInput->setValue(kVolumeDefaultPercent);
    m_volumeInput->setSuffix(QStringLiteral("%"));
    m_volumeInput->setAlignment(Qt::AlignRight);
    m_volumeInput->setMinimumWidth(72);
    auto* volRow = new QHBoxLayout();
    volRow->addWidget(m_volume, 1);
    volRow->addWidget(m_volumeInput);
    m_volumeTitle = new QLabel(central);
    form->addRow(m_volumeTitle, volRow);

    root->addLayout(form);

    m_status = new QLabel(central);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setMinimumHeight(40);
    root->addWidget(m_status);

    m_startStop = new QPushButton(central);
    m_startStop->setMinimumHeight(36);
    root->addWidget(m_startStop);

    setCentralWidget(central);
    resize(520, sizeHint().height());

    m_tray = new QSystemTrayIcon(makeAppIcon(), this);
    m_trayMenu = new QMenu(this);
    m_trayShow = new QAction(this);
    m_trayStartStop = new QAction(this);
    m_trayMenu->addAction(m_trayShow);
    m_trayMenu->addAction(m_trayStartStop);
    m_trayMenu->addSeparator();
    m_languageMenu = new QMenu(this);
    m_trayMenu->addMenu(m_languageMenu);
    m_trayMenu->addSeparator();
    m_trayQuit = new QAction(this);
    m_trayMenu->addAction(m_trayQuit);
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);
    m_languageEnglish = new QAction(this);
    m_languageChinese = new QAction(this);
    m_languageGroup->addAction(m_languageEnglish);
    m_languageGroup->addAction(m_languageChinese);
    m_languageMenu->addAction(m_languageEnglish);
    m_languageMenu->addAction(m_languageChinese);
    m_languageEnglish->setCheckable(true);
    m_languageChinese->setCheckable(true);
    m_tray->setContextMenu(m_trayMenu);
    m_tray->show();

    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_volume, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_volumeInput, qOverload<int>(&QSpinBox::valueChanged), this,
            &MainWindow::onVolumeInputChanged);
    connect(m_startStop, &QPushButton::clicked, this, &MainWindow::startStopClicked);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);
    connect(m_trayShow, &QAction::triggered, this, &MainWindow::showMainWindow);
    connect(m_trayStartStop, &QAction::triggered, this, &MainWindow::startStopClicked);
    connect(m_trayQuit, &QAction::triggered, this, &MainWindow::quitApp);
    connect(m_languageEnglish, &QAction::triggered, this, &MainWindow::selectEnglish);
    connect(m_languageChinese, &QAction::triggered, this, &MainWindow::selectSimplifiedChinese);

    retranslateUi();
}

QString MainWindow::defaultLanguage() const
{
    return QLocale::system().language() == QLocale::Chinese
        ? QString::fromLatin1(kSimplifiedChineseLanguage)
        : QString::fromLatin1(kEnglishLanguage);
}

QString MainWindow::configuredLanguage() const
{
    QSettings settings;
    const QString configured = settings.value(QString::fromLatin1(kLanguageSetting)).toString();
    if (configured == QLatin1String(kEnglishLanguage)
        || configured == QLatin1String(kSimplifiedChineseLanguage)) {
        return configured;
    }
    return defaultLanguage();
}

bool MainWindow::installLanguage(const QString& language, bool persist)
{
    const QString normalized = language == QLatin1String(kSimplifiedChineseLanguage)
        ? QString::fromLatin1(kSimplifiedChineseLanguage)
        : QString::fromLatin1(kEnglishLanguage);
    const QString path = QStringLiteral(":/translations/audiomonitor_%1.qm").arg(normalized);

    // Validate the catalog before removing the currently active one. English
    // may safely fall back to source strings if its catalog is unavailable.
    QTranslator candidate;
    const bool loaded = candidate.load(path);
    if (normalized == QLatin1String(kSimplifiedChineseLanguage) && !loaded) {
        updateLanguageActions();
        return false;
    }

    if (m_translator) {
        qApp->removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    if (loaded) {
        m_translator = new QTranslator(this);
        if (m_translator->load(path))
            qApp->installTranslator(m_translator);
    }

    m_languageCode = normalized;
    if (persist) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kLanguageSetting), m_languageCode);
        settings.sync();
    }
    retranslateUi();
    return true;
}

void MainWindow::selectEnglish()
{
    installLanguage(QString::fromLatin1(kEnglishLanguage), true);
}

void MainWindow::selectSimplifiedChinese()
{
    installLanguage(QString::fromLatin1(kSimplifiedChineseLanguage), true);
}

void MainWindow::updateLanguageActions()
{
    if (m_languageEnglish)
        m_languageEnglish->setChecked(m_languageCode == QLatin1String(kEnglishLanguage));
    if (m_languageChinese)
        m_languageChinese->setChecked(m_languageCode == QLatin1String(kSimplifiedChineseLanguage));
}

void MainWindow::retranslateUi()
{
    setWindowTitle(QStringLiteral("Audio Monitor"));
    if (m_sourceTitle)
        m_sourceTitle->setText(tr("Listen source (output device):"));
    if (m_targetTitle)
        m_targetTitle->setText(tr("Forward to (output device):"));
    if (m_volumeTitle)
        m_volumeTitle->setText(tr("Monitoring volume:"));
    if (m_refresh)
        m_refresh->setText(tr("Refresh devices"));
    if (m_hint) {
        m_hint->setText(tr("The listen source is the output device that is currently playing audio. "
                           "Its audio is forwarded to the target device in real time."));
    }
    if (m_startStop)
        m_startStop->setText(m_running ? tr("Stop monitoring") : tr("Start monitoring"));
    if (m_trayShow)
        m_trayShow->setText(tr("Open main window"));
    if (m_trayStartStop)
        m_trayStartStop->setText(m_running ? tr("Stop monitoring") : tr("Start monitoring"));
    if (m_trayQuit)
        m_trayQuit->setText(tr("Quit"));
    if (m_languageMenu)
        m_languageMenu->setTitle(tr("Language"));
    if (m_languageEnglish)
        m_languageEnglish->setText(tr("English"));
    if (m_languageChinese)
        m_languageChinese->setText(tr("Simplified Chinese"));
    updateLanguageActions();

    const QString defaultSuffix = tr(" (default)");
    const auto updateLabels = [&defaultSuffix](QComboBox* box) {
        if (!box)
            return;
        for (int i = 0; i < box->count(); ++i) {
            const QVariant nameData = box->itemData(i, kDeviceNameRole);
            if (!nameData.isValid())
                continue;
            QString label = nameData.toString();
            if (box->itemData(i, kDeviceDefaultRole).toBool())
                label += defaultSuffix;
            box->setItemText(i, label);
        }
    };
    updateLabels(m_source);
    updateLabels(m_target);

    // Rebuild state-dependent messages in the newly selected language.
    if (m_reconnectTimer && m_reconnectTimer->isActive())
        updateReconnectStatus();
    else
        updateUiState();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void MainWindow::refreshDevices()
{
    const QString srcId = m_source->currentData().toString();
    const QString tgtId = m_target->currentData().toString();

    const QVector<DeviceInfo> devices = m_router->outputDevices();
    const QString defaultSuffix = tr(" (default)");

    auto fill = [&defaultSuffix](QComboBox* box,
                                 const QVector<DeviceInfo>& list,
                                 const QString& prevId) {
        box->blockSignals(true);
        box->clear();
        for (const DeviceInfo& d : list) {
            QString label = d.name;
            if (d.isDefault)
                label += defaultSuffix;
            box->addItem(label, d.id);
            const int index = box->count() - 1;
            box->setItemData(index, d.name, kDeviceNameRole);
            box->setItemData(index, d.isDefault, kDeviceDefaultRole);
        }
        int idx = -1;
        if (!prevId.isEmpty())
            idx = box->findData(prevId);
        if (idx < 0 && box->count() > 0)
            idx = 0;
        if (idx >= 0)
            box->setCurrentIndex(idx);
        box->blockSignals(false);
    };
    fill(m_source, devices, srcId);
    fill(m_target, devices, tgtId);

    if (!m_running)
        updateUiState();
}

void MainWindow::applySavedSelection(QComboBox* box, const QString& id, const QString& name)
{
    if (box->count() == 0)
        return;
    int idx = -1;
    if (!id.isEmpty())
        idx = box->findData(id);
    if (idx < 0 && !name.isEmpty()) {
        for (int i = 0; i < box->count(); ++i) {
            const QString itemName = box->itemData(i, kDeviceNameRole).toString();
            if (itemName == name || (box->itemData(i, kDeviceDefaultRole).toBool()
                                     && (name == itemName + QStringLiteral(" (default)")
                                         || name == itemName + QStringLiteral("\uFF08\u9ED8\u8BA4\uFF09")))) {
                idx = i;
                break;
            }
        }
    }
    if (idx >= 0)
        box->setCurrentIndex(idx);
}

void MainWindow::startStopClicked()
{
    if (m_running) {
        cancelReconnect();
        m_router->stop();
        return;
    }
    cancelReconnect(); // A manual start takes precedence over pending recovery.
    m_lastError.clear();
    const QString srcId = m_source->currentData().toString();
    const QString tgtId = m_target->currentData().toString();
    if (srcId.isEmpty() || tgtId.isEmpty()) {
        m_status->setText(colorStatus(QStringLiteral("#c0392b"),
                                       tr("Error: no output device is available. Check the audio service.")));
        return;
    }
    if (srcId == tgtId) {
        m_status->setText(colorStatus(
            QStringLiteral("#c0392b"),
            tr("The source and target must be different to prevent an audio feedback loop.")));
        return;
    }
    const float volume = m_volumeInput->value() / kVolumePercentScale;
    m_router->start(srcId, tgtId, volume); // Completion is reported by signals.
}

void MainWindow::onStarted()
{
    m_running = true;
    m_lastError.clear();

    // Reflect the devices that the backend actually started. This keeps status
    // text and persisted selections correct after an automatic reconnect.
    const SessionDeviceIds ids = m_router ? m_router->lastSessionDeviceIds()
                                          : SessionDeviceIds{};
    if ((!ids.sourceId.isEmpty() && m_source->findData(ids.sourceId) < 0)
        || (!ids.targetId.isEmpty() && m_target->findData(ids.targetId) < 0)) {
        refreshDevices();
    }
    const auto selectDevice = [](QComboBox* box, const QString& id) {
        if (!box || id.isEmpty())
            return;
        const int index = box->findData(id);
        if (index >= 0)
            box->setCurrentIndex(index);
    };
    selectDevice(m_source, ids.sourceId);
    selectDevice(m_target, ids.targetId);

    cancelReconnect();
    updateUiState();
    saveSettings();
}

void MainWindow::onStopped(StopReason reason)
{
    const bool wasRunning = m_running;
    m_running = false;

    // Only device/service failures trigger automatic reconnect.
    if (reason == StopReason::UserRequested) {
        cancelReconnect();
        if (wasRunning)
            updateUiState();
        return;
    }

    // Use the IDs captured by the backend for this session. The combo boxes
    // may have been refreshed before this signal is delivered.
    if (m_reconnectSourceId.isEmpty() || m_reconnectTargetId.isEmpty()) {
        const SessionDeviceIds ids = m_router ? m_router->lastSessionDeviceIds()
                                              : SessionDeviceIds{};
        if (!ids.sourceId.isEmpty() && !ids.targetId.isEmpty()) {
            m_reconnectSourceId = ids.sourceId;
            m_reconnectTargetId = ids.targetId;
        }
    }
    scheduleReconnect();
}

void MainWindow::scheduleReconnect()
{
    if (m_quitting || m_reconnectSourceId.isEmpty() || m_reconnectTargetId.isEmpty()) {
        cancelReconnect();
        updateUiState();
        return;
    }
    if (m_reconnectAttempt >= kMaxReconnectAttempts) {
        cancelReconnect();
        updateUiState();
        m_status->setText(colorStatus(
            QStringLiteral("#c0392b"),
            tr("The device is disconnected. Automatic reconnect failed; check the device and try again.")));
        return;
    }

    const int delay = kReconnectDelaysMs[m_reconnectAttempt];
    ++m_reconnectAttempt;
    m_reconnectDelayMs = delay;
    m_reconnectTimer->start(delay);
    updateReconnectStatus();
    if (m_tray)
        m_tray->setToolTip(tr("AudioMonitor - Reconnecting"));
}

void MainWindow::updateReconnectStatus()
{
    if (!m_status || m_reconnectAttempt <= 0)
        return;
    m_status->setText(colorStatus(
        QStringLiteral("#d35400"),
        tr("Device disconnected; retrying in %1 seconds (attempt %2/%3)...")
            .arg(m_reconnectDelayMs / 1000.0, 0, 'g', 2)
            .arg(m_reconnectAttempt)
            .arg(kMaxReconnectAttempts)));
}

void MainWindow::cancelReconnect()
{
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_reconnectDelayMs = 0;
    m_reconnectSourceId.clear();
    m_reconnectTargetId.clear();
}

void MainWindow::attemptReconnect()
{
    if (m_quitting || m_running)
        return;

    // Wait until both IDs captured from the failed session are present again.
    const QVector<DeviceInfo> devices = m_router->outputDevices();
    const auto hasDevice = [&devices](const QString& id) {
        for (const DeviceInfo& d : devices) {
            if (d.id == id)
                return true;
        }
        return false;
    };
    if (!hasDevice(m_reconnectSourceId) || !hasDevice(m_reconnectTargetId)) {
        scheduleReconnect(); // Keep waiting until both devices are present.
        return;
    }

    const float volume = m_volumeInput->value() / kVolumePercentScale;
    if (!m_router->start(m_reconnectSourceId, m_reconnectTargetId, volume))
        scheduleReconnect(); // start() reports the error; schedule the next attempt.
}

void MainWindow::onError(const QString& message)
{
    m_lastError = message;
    m_status->setText(colorStatus(QStringLiteral("#c0392b"),
                                  tr("Error: %1").arg(message.toHtmlEscaped())));
    if (m_tray)
        m_tray->setToolTip(tr("AudioMonitor - Error"));
}

void MainWindow::onVolumeChanged(int value)
{
    applyVolumeValue(VolumeSlider::volumeForPosition(value));
}

void MainWindow::onVolumeInputChanged(int value)
{
    const int position = VolumeSlider::positionForVolume(value);
    if (m_volume && m_volume->value() != position) {
        const QSignalBlocker blocker(m_volume);
        m_volume->setValue(position);
    }
    applyVolumeValue(value);
}

void MainWindow::applyVolumeValue(int value)
{
    if (m_volumeInput && m_volumeInput->value() != value) {
        const QSignalBlocker blocker(m_volumeInput);
        m_volumeInput->setValue(value);
    }
    if (m_router)
        m_router->setVolume(value / kVolumePercentScale);
    saveSettings();
}

void MainWindow::updateUiState()
{
    const QString srcName = m_source->currentText();
    const QString tgtName = m_target->currentText();

    if (m_running) {
        m_startStop->setText(tr("Stop monitoring"));
        m_trayStartStop->setText(tr("Stop monitoring"));
        m_source->setEnabled(false);
        m_target->setEnabled(false);
        m_refresh->setEnabled(false);
        m_status->setText(colorStatus(
            QStringLiteral("#27ae60"),
            tr("Monitoring: %1 -> %2 (continues in the background when the window is closed)")
                .arg(srcName.toHtmlEscaped(), tgtName.toHtmlEscaped())));
        if (m_tray)
            m_tray->setToolTip(tr("AudioMonitor - Running: %1 -> %2").arg(srcName, tgtName));
    } else {
        m_startStop->setText(tr("Start monitoring"));
        m_trayStartStop->setText(tr("Start monitoring"));
        m_source->setEnabled(true);
        m_target->setEnabled(true);
        m_refresh->setEnabled(true);
        const bool reconnectPending = m_reconnectTimer && m_reconnectTimer->isActive();
        if (reconnectPending) {
            updateReconnectStatus();
            if (m_tray)
                m_tray->setToolTip(tr("AudioMonitor - Reconnecting"));
        } else if (m_source->count() == 0) {
            m_status->setText(colorStatus(
                QStringLiteral("#c0392b"),
                tr("No output devices found. Make sure the audio service (Windows Audio or PipeWire) "
                   "is running, then refresh the device list.")));
        } else if (!m_lastError.isEmpty()) {
            m_status->setText(colorStatus(
                QStringLiteral("#c0392b"), tr("Error: %1").arg(m_lastError.toHtmlEscaped())));
        } else {
            m_status->setText(colorStatus(
                QStringLiteral("#7f8c8d"),
                tr("Stopped. Select devices and click Start monitoring.")));
        }
        if (m_tray && !reconnectPending)
            m_tray->setToolTip(tr("AudioMonitor - Stopped"));
    }
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        showMainWindow();
}

void MainWindow::showMainWindow()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::hideToTray()
{
    hide();
}

void MainWindow::quitApp()
{
    m_quitting = true;
    cancelReconnect();
    saveSettings();
    if (m_router)
        m_router->stop();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // With a tray icon, closing the window keeps forwarding in the background.
    if (!m_quitting && QSystemTrayIcon::isSystemTrayAvailable()) {
        hideToTray();
        event->ignore();
        return;
    }
    m_quitting = true;
    cancelReconnect();
    saveSettings();
    if (m_router)
        m_router->stop();
    QApplication::quit();
    event->accept();
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_volumeInput->setValue(s.value(QStringLiteral("volume"), kVolumeDefaultPercent).toInt());
    m_savedSourceId = s.value(QStringLiteral("source/id")).toString();
    m_savedSourceName = s.value(QStringLiteral("source/name")).toString();
    m_savedTargetId = s.value(QStringLiteral("target/id")).toString();
    m_savedTargetName = s.value(QStringLiteral("target/name")).toString();
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("volume"), m_volumeInput->value());
    const auto currentDeviceName = [](QComboBox* box) {
        const int index = box ? box->currentIndex() : -1;
        const QVariant nameData = index >= 0 ? box->itemData(index, kDeviceNameRole) : QVariant{};
        return nameData.isValid() ? nameData.toString() : (box ? box->currentText() : QString{});
    };
    if (!m_source->currentData().toString().isEmpty()) {
        s.setValue(QStringLiteral("source/id"), m_source->currentData());
        s.setValue(QStringLiteral("source/name"), currentDeviceName(m_source));
    }
    if (!m_target->currentData().toString().isEmpty()) {
        s.setValue(QStringLiteral("target/id"), m_target->currentData());
        s.setValue(QStringLiteral("target/name"), currentDeviceName(m_target));
    }
}
