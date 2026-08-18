#include "mainwindow.h"

#include "appicon.h"
#include "core/audiorouter.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

#include <iterator>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// 重连退避序列（毫秒）与上限：设备重新枚举通常需要 1~2 秒。
constexpr int kReconnectDelaysMs[] = {1000, 2000, 4000, 8000, 15000};
constexpr int kMaxReconnectAttempts = int(std::size(kReconnectDelaysMs));
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("音频监听转发 — AudioMonitor"));
    setWindowIcon(makeAppIcon());

    m_router = AudioRouter::create(this);

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

    // 应用持久化的选择（第一次 refreshDevices 已按 id 尝试恢复）
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

    // 设备选择
    m_source = new QComboBox(central);
    m_source->setMinimumWidth(320);
    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(m_source, 1);
    m_refresh = new QPushButton(QStringLiteral("刷新设备"), central);
    sourceRow->addWidget(m_refresh);
    form->addRow(QStringLiteral("监听源（输出设备）："), sourceRow);

    m_target = new QComboBox(central);
    m_target->setMinimumWidth(320);
    form->addRow(QStringLiteral("转发到（输出设备）："), m_target);

    auto* hint = new QLabel(
        QStringLiteral("监听源是「正在播放声音的输出设备」，其音频将被实时转发到目标设备。"),
        central);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #666; font-size: 12px;"));
    form->addRow(QString(), hint);

    // 音量
    m_volume = new QSlider(Qt::Horizontal, central);
    m_volume->setRange(0, 200);
    m_volume->setValue(100);
    m_volume->setTickPosition(QSlider::TicksBelow);
    m_volume->setTickInterval(50);
    m_volumeLabel = new QLabel(QStringLiteral("100%"), central);
    m_volumeLabel->setMinimumWidth(44);
    auto* volRow = new QHBoxLayout();
    volRow->addWidget(m_volume, 1);
    volRow->addWidget(m_volumeLabel);
    form->addRow(QStringLiteral("监听音量："), volRow);

    root->addLayout(form);

    // 状态 + 启停
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

    // 系统托盘
    m_tray = new QSystemTrayIcon(makeAppIcon(), this);
    m_trayMenu = new QMenu(this);
    m_trayShow = m_trayMenu->addAction(QStringLiteral("打开主窗口"));
    m_trayStartStop = m_trayMenu->addAction(QStringLiteral("开始监听"));
    m_trayMenu->addSeparator();
    m_trayQuit = m_trayMenu->addAction(QStringLiteral("退出"));
    m_tray->setContextMenu(m_trayMenu);
    m_tray->show();

    // 连接
    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_volume, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_startStop, &QPushButton::clicked, this, &MainWindow::startStopClicked);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);
    connect(m_trayShow, &QAction::triggered, this, &MainWindow::showMainWindow);
    connect(m_trayStartStop, &QAction::triggered, this, &MainWindow::startStopClicked);
    connect(m_trayQuit, &QAction::triggered, this, &MainWindow::quitApp);
}

void MainWindow::refreshDevices()
{
    const QString srcId = m_source->currentData().toString();
    const QString tgtId = m_target->currentData().toString();

    const QVector<DeviceInfo> devices = m_router->outputDevices();

    auto fill = [](QComboBox* box, const QVector<DeviceInfo>& list, const QString& prevId) {
        box->blockSignals(true);
        box->clear();
        for (const DeviceInfo& d : list) {
            QString label = d.name;
            if (d.isDefault)
                label += QStringLiteral("（默认）");
            box->addItem(label, d.id);
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
            QString text = box->itemText(i);
            if (text.endsWith(QStringLiteral("（默认）")))
                text.chop(4);
            if (text == name) {
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
    cancelReconnect(); // 手动启动优先于待执行的自动重连
    const QString srcId = m_source->currentData().toString();
    const QString tgtId = m_target->currentData().toString();
    if (srcId.isEmpty() || tgtId.isEmpty()) {
        m_status->setText(
            QStringLiteral("<span style='color:#c0392b;'>错误：没有可用的输出设备，请检查音频服务。</span>"));
        return;
    }
    const float volume = m_volume->value() / 100.0f;
    m_router->start(srcId, tgtId, volume); // 结果通过 started/errorOccurred 信号反馈
}

void MainWindow::onStarted()
{
    m_running = true;
    cancelReconnect();
    updateUiState();
    saveSettings();
}

void MainWindow::onStopped(StopReason reason)
{
    const bool wasRunning = m_running;
    m_running = false;

    // 只有设备/服务故障才自动重连；用户主动停止时清掉待重连状态。
    if (reason == StopReason::UserRequested) {
        cancelReconnect();
        if (wasRunning)
            updateUiState();
        return;
    }

    // 记住故障发生时的设备，避免重连期间下拉框被刷新改写。
    if (m_reconnectSourceId.isEmpty()) {
        m_reconnectSourceId = m_source->currentData().toString();
        m_reconnectTargetId = m_target->currentData().toString();
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
        m_status->setText(QStringLiteral(
            "<span style='color:#c0392b;'>设备已断开，多次重连失败，请检查设备后手动重试。</span>"));
        return;
    }

    const int delay = kReconnectDelaysMs[m_reconnectAttempt];
    ++m_reconnectAttempt;
    m_reconnectTimer->start(delay);
    m_status->setText(QStringLiteral("<span style='color:#d35400;'>设备已断开，%1 秒后尝试重连"
                                     "（第 %2/%3 次）…</span>")
                          .arg(delay / 1000.0, 0, 'g', 2)
                          .arg(m_reconnectAttempt)
                          .arg(kMaxReconnectAttempts));
    if (m_tray)
        m_tray->setToolTip(QStringLiteral("音频监听转发 — 正在重连"));
}

void MainWindow::cancelReconnect()
{
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_reconnectSourceId.clear();
    m_reconnectTargetId.clear();
}

void MainWindow::attemptReconnect()
{
    if (m_quitting || m_running)
        return;

    // 设备可能换了 id（重新插入），先确认目标仍在当前设备列表里。
    const QVector<DeviceInfo> devices = m_router->outputDevices();
    const auto hasDevice = [&devices](const QString& id) {
        for (const DeviceInfo& d : devices) {
            if (d.id == id)
                return true;
        }
        return false;
    };
    if (!hasDevice(m_reconnectSourceId) || !hasDevice(m_reconnectTargetId)) {
        scheduleReconnect(); // 设备还没回来，继续退避等待
        return;
    }

    const float volume = m_volume->value() / 100.0f;
    if (!m_router->start(m_reconnectSourceId, m_reconnectTargetId, volume))
        scheduleReconnect(); // start() 失败会走 errorOccurred，这里只安排下一次
}

void MainWindow::onError(const QString& message)
{
    m_status->setText(QStringLiteral("<span style='color:#c0392b;'>错误：%1</span>")
                          .arg(message.toHtmlEscaped()));
    if (m_tray)
        m_tray->setToolTip(QStringLiteral("音频监听转发 — 出错"));
}

void MainWindow::onVolumeChanged(int value)
{
    m_volumeLabel->setText(QStringLiteral("%1%").arg(value));
    if (m_router)
        m_router->setVolume(value / 100.0f);
    saveSettings();
}

void MainWindow::updateUiState()
{
    const QString srcName = m_source->currentText();
    const QString tgtName = m_target->currentText();

    if (m_running) {
        m_startStop->setText(QStringLiteral("停止监听"));
        m_trayStartStop->setText(QStringLiteral("停止监听"));
        m_source->setEnabled(false);
        m_target->setEnabled(false);
        m_refresh->setEnabled(false);
        m_status->setText(QStringLiteral("<span style='color:#27ae60;'>● 正在监听：%1 → %2"
                                         "（关闭窗口后仍在后台运行）</span>")
                              .arg(srcName.toHtmlEscaped(), tgtName.toHtmlEscaped()));
        if (m_tray)
            m_tray->setToolTip(
                QStringLiteral("音频监听转发 — 运行中：%1 → %2").arg(srcName, tgtName));
    } else {
        m_startStop->setText(QStringLiteral("开始监听"));
        m_trayStartStop->setText(QStringLiteral("开始监听"));
        m_source->setEnabled(true);
        m_target->setEnabled(true);
        m_refresh->setEnabled(true);
        const bool reconnectPending = m_reconnectTimer && m_reconnectTimer->isActive();
        if (reconnectPending) {
            // 保留 scheduleReconnect() 写入的重连提示，不被设备刷新覆盖。
        } else if (m_source->count() == 0) {
            m_status->setText(QStringLiteral(
                "<span style='color:#c0392b;'>未找到输出设备。"
                "请确认音频服务（Windows Audio / PulseAudio / PipeWire）正在运行，"
                "然后点击「刷新设备」。</span>"));
        } else if (!m_status->text().startsWith(QStringLiteral("<span style='color:#c0392b;'>错误"))) {
            m_status->setText(QStringLiteral("<span style='color:#7f8c8d;'>已停止。选择设备后点击「开始监听」。</span>"));
        }
        if (m_tray)
            m_tray->setToolTip(QStringLiteral("音频监听转发 — 已停止"));
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
    if (!m_trayMessageShown) {
        m_trayMessageShown = true;
        if (m_tray && QSystemTrayIcon::supportsMessages()) {
            m_tray->showMessage(QStringLiteral("音频监听转发"),
                                QStringLiteral("程序已最小化到系统托盘，监听在后台继续运行。"),
                                QSystemTrayIcon::Information, 3000);
        }
    }
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
    // 有托盘时关闭窗口 = 最小化到托盘继续后台运行；否则直接退出
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
    m_volume->setValue(s.value(QStringLiteral("volume"), 100).toInt());
    m_volumeLabel->setText(QStringLiteral("%1%").arg(m_volume->value()));
    m_savedSourceId = s.value(QStringLiteral("source/id")).toString();
    m_savedSourceName = s.value(QStringLiteral("source/name")).toString();
    m_savedTargetId = s.value(QStringLiteral("target/id")).toString();
    m_savedTargetName = s.value(QStringLiteral("target/name")).toString();
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("volume"), m_volume->value());
    if (!m_source->currentData().toString().isEmpty()) {
        s.setValue(QStringLiteral("source/id"), m_source->currentData());
        s.setValue(QStringLiteral("source/name"), m_source->currentText());
    }
    if (!m_target->currentData().toString().isEmpty()) {
        s.setValue(QStringLiteral("target/id"), m_target->currentData());
        s.setValue(QStringLiteral("target/name"), m_target->currentText());
    }
}
