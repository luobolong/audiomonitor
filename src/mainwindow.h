#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>

#include "core/audiorouter.h"

class QTimer;
class QComboBox;
class QLabel;
class QSlider;
class QPushButton;
class QMenu;
class QAction;
class QCloseEvent;

// 主窗口：选择监听源/转发目标输出设备、音量、启停；
// 关闭窗口时最小化到系统托盘，转发继续在后台运行。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refreshDevices();
    void startStopClicked();
    void onStarted();
    void onStopped(StopReason reason);
    void onError(const QString& message);
    void onVolumeChanged(int value);
    void attemptReconnect();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void showMainWindow();
    void quitApp();

private:
    void buildUi();
    void updateUiState();
    // 设备/服务故障后安排一次退避重连；超过重试上限则放弃并提示。
    void scheduleReconnect();
    void cancelReconnect();
    void loadSettings();
    void saveSettings();
    // 依据持久化的 id（回退名称）恢复下拉框选择
    void applySavedSelection(QComboBox* box, const QString& id, const QString& name);
    void hideToTray();

    AudioRouter* m_router = nullptr;

    QComboBox* m_source = nullptr;
    QComboBox* m_target = nullptr;
    QSlider* m_volume = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_startStop = nullptr;
    QPushButton* m_refresh = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_trayShow = nullptr;
    QAction* m_trayStartStop = nullptr;
    QAction* m_trayQuit = nullptr;

    bool m_running = false;
    bool m_quitting = false;
    bool m_trayMessageShown = false;

    // 自动重连：仅在 StopReason 为设备/服务故障时启动。
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectAttempt = 0;
    QString m_reconnectSourceId;
    QString m_reconnectTargetId;

    // 待恢复的选择（首次刷新设备后应用）
    QString m_savedSourceId;
    QString m_savedSourceName;
    QString m_savedTargetId;
    QString m_savedTargetName;
};
