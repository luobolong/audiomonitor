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
class QActionGroup;
class QCloseEvent;
class QEvent;
class QTranslator;

// Main window for selecting the source/target devices, volume, and run state.
// Closing the window hides it in the system tray while forwarding continues.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(AudioRouter* router, QWidget* parent);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

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
    void selectEnglish();
    void selectSimplifiedChinese();

private:
    void buildUi();
    void retranslateUi();
    void updateUiState();
    // Schedule one exponential-backoff reconnect after a device/service fault.
    void scheduleReconnect();
    void cancelReconnect();
    void loadSettings();
    void saveSettings();
    // Restore a combo-box selection by persisted ID, falling back to its name.
    void applySavedSelection(QComboBox* box, const QString& id, const QString& name);
    void hideToTray();
    QString configuredLanguage() const;
    QString defaultLanguage() const;
    bool installLanguage(const QString& language, bool persist);
    void updateLanguageActions();
    void updateReconnectStatus();

    AudioRouter* m_router = nullptr;

    QComboBox* m_source = nullptr;
    QComboBox* m_target = nullptr;
    QSlider* m_volume = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QLabel* m_sourceTitle = nullptr;
    QLabel* m_targetTitle = nullptr;
    QLabel* m_volumeTitle = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_startStop = nullptr;
    QPushButton* m_refresh = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_trayShow = nullptr;
    QAction* m_trayStartStop = nullptr;
    QAction* m_trayQuit = nullptr;
    QMenu* m_languageMenu = nullptr;
    QAction* m_languageEnglish = nullptr;
    QAction* m_languageChinese = nullptr;
    QActionGroup* m_languageGroup = nullptr;
    QTranslator* m_translator = nullptr;

    bool m_running = false;
    bool m_quitting = false;
    bool m_trayMessageShown = false;

    // Automatic reconnect is used only for device/service failures.
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectAttempt = 0;
    int m_reconnectDelayMs = 0;
    QString m_reconnectSourceId;
    QString m_reconnectTargetId;
    QString m_lastError;
    QString m_languageCode;

    // Saved selections applied after the first device refresh.
    QString m_savedSourceId;
    QString m_savedSourceName;
    QString m_savedTargetId;
    QString m_savedTargetName;
};
