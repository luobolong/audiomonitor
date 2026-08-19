#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QMenu>
#include <QLocale>
#include <QPushButton>
#include <QSlider>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool invoke(MainWindow& window, const char* method)
{
    return QMetaObject::invokeMethod(&window, method, Qt::DirectConnection);
}

class FakeRouter final : public AudioRouter {
public:
    struct StartRequest {
        QString sourceId;
        QString targetId;
        float volume = 0.0f;
    };

    explicit FakeRouter(QObject* parent = nullptr)
        : AudioRouter(parent)
    {
    }

    QVector<DeviceInfo> outputDevices() override { return devices; }

    bool start(const QString& sourceId, const QString& targetId, float volume) override
    {
        requests.push_back({ sourceId, targetId, volume });
        if (sourceId.isEmpty() || targetId.isEmpty() || sourceId == targetId)
            return false;
        m_sessionIds = { sourceId, targetId };
        m_running = true;
        emit started();
        return true;
    }

    void stop() override
    {
        if (!m_running)
            return;
        m_running = false;
        m_sessionIds = {};
        ++stopCount;
        emit stopped(StopReason::UserRequested);
    }

    bool isRunning() const override { return m_running; }
    SessionDeviceIds lastSessionDeviceIds() const override { return m_sessionIds; }
    void setVolume(float volume) override { lastVolume = volume; }

    void refresh()
    {
        emit deviceListChanged();
    }

    void fail(StopReason reason)
    {
        m_running = false;
        emit stopped(reason);
    }

    QVector<DeviceInfo> devices;
    std::vector<StartRequest> requests;
    int stopCount = 0;
    float lastVolume = 1.0f;

private:
    bool m_running = false;
    SessionDeviceIds m_sessionIds;
};

QComboBox* sourceBox(MainWindow& window)
{
    return window.findChild<QComboBox*>(QStringLiteral("sourceDeviceCombo"));
}

QComboBox* targetBox(MainWindow& window)
{
    return window.findChild<QComboBox*>(QStringLiteral("targetDeviceCombo"));
}

QMenu* languageMenu(MainWindow& window, const QString& title)
{
    for (QMenu* menu : window.findChildren<QMenu*>()) {
        if (menu->title() == title)
            return menu;
    }
    return nullptr;
}

QAction* menuAction(QMenu* menu, const QString& text)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

QAction* windowAction(MainWindow& window, const QString& text)
{
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

bool hasButtonText(MainWindow& window, const QString& text)
{
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == text)
            return true;
    }
    return false;
}

bool hasLabelText(MainWindow& window, const QString& text)
{
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text() == text)
            return true;
    }
    return false;
}

void setLanguageSetting(const QString& language)
{
    QSettings settings;
    if (language.isEmpty())
        settings.remove(QStringLiteral("ui/language"));
    else
        settings.setValue(QStringLiteral("ui/language"), language);
    settings.sync();
}

void clearSavedDeviceSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("source/id"));
    settings.remove(QStringLiteral("source/name"));
    settings.remove(QStringLiteral("target/id"));
    settings.remove(QStringLiteral("target/name"));
    settings.sync();
}

void testRefreshCannotOverwriteSessionIds()
{
    auto* router = new FakeRouter;
    router->devices = {
        { QStringLiteral("source-a"), QStringLiteral("Source A"), true },
        { QStringLiteral("target-b"), QStringLiteral("Target B"), false },
    };
    MainWindow window(router, nullptr);
    QComboBox* source = sourceBox(window);
    QComboBox* target = targetBox(window);
    expect(source && target, "device combo boxes are discoverable for tests");
    if (!source || !target)
        return;

    source->setCurrentIndex(0);
    target->setCurrentIndex(1);
    expect(invoke(window, "startStopClicked"), "initial start slot is invokable");
    expect(router->requests.size() == 1, "initial session starts once");
    expect(router->requests.back().sourceId == QStringLiteral("source-a")
               && router->requests.back().targetId == QStringLiteral("target-b"),
           "initial session uses selected IDs");

    // Reproduce the problematic ordering: refresh the GUI to unrelated
    // devices first, then report that the active session failed.
    router->devices = {
        { QStringLiteral("source-c"), QStringLiteral("Source C"), true },
        { QStringLiteral("target-d"), QStringLiteral("Target D"), false },
    };
    router->refresh();
    target->setCurrentIndex(1);
    expect(source->currentData().toString() == QStringLiteral("source-c")
               && target->currentData().toString() == QStringLiteral("target-d"),
           "device refresh changes the visible selections before failure");
    router->fail(StopReason::DeviceFailure);

    // Make the original devices available again without changing the visible
    // C/D selections, then trigger the pending reconnect immediately.
    router->devices = {
        { QStringLiteral("source-a"), QStringLiteral("Source A"), true },
        { QStringLiteral("target-b"), QStringLiteral("Target B"), false },
        { QStringLiteral("source-c"), QStringLiteral("Source C"), false },
        { QStringLiteral("target-d"), QStringLiteral("Target D"), false },
    };
    expect(invoke(window, "attemptReconnect"), "reconnect slot is invokable");
    expect(router->requests.size() == 2, "reconnect starts one replacement session");
    expect(router->requests.back().sourceId == QStringLiteral("source-a")
               && router->requests.back().targetId == QStringLiteral("target-b"),
           "reconnect uses backend session IDs instead of refreshed GUI IDs");
    expect(source->currentData().toString() == QStringLiteral("source-a")
               && target->currentData().toString() == QStringLiteral("target-b"),
           "successful reconnect realigns the GUI with the active session");
}

void testRepeatedStartStop()
{
    auto* router = new FakeRouter;
    router->devices = {
        { QStringLiteral("source"), QStringLiteral("Source"), true },
        { QStringLiteral("target"), QStringLiteral("Target"), false },
    };
    MainWindow window(router, nullptr);
    QComboBox* source = sourceBox(window);
    QComboBox* target = targetBox(window);
    if (!source || !target) {
        expect(false, "repeated start/stop has device combo boxes");
        return;
    }
    source->setCurrentIndex(0);
    target->setCurrentIndex(1);

    for (int iteration = 0; iteration < 25; ++iteration) {
        expect(invoke(window, "startStopClicked"), "repeated start is invokable");
        expect(router->isRunning(), "router is running after each start");
        expect(invoke(window, "startStopClicked"), "repeated stop is invokable");
        expect(!router->isRunning(), "router is stopped after each stop");
    }
    expect(router->requests.size() == 25, "every repeated start reaches the router once");
    expect(router->stopCount == 25, "every repeated stop reaches the router once");
}

void testInvalidAndIdenticalSelections()
{
    {
        auto* router = new FakeRouter;
        MainWindow window(router, nullptr);
        expect(invoke(window, "startStopClicked"), "empty-selection start is invokable");
        expect(router->requests.empty(), "empty selections never reach the router");
    }

    {
        auto* router = new FakeRouter;
        router->devices = {
            { QStringLiteral("same"), QStringLiteral("Only device"), true },
        };
        MainWindow window(router, nullptr);
        expect(invoke(window, "startStopClicked"), "identical-selection start is invokable");
        expect(router->requests.empty(), "identical source and target are rejected in the GUI");
    }
}

void testVolumeRange()
{
    auto* router = new FakeRouter;
    router->devices = {
        { QStringLiteral("source"), QStringLiteral("Source"), true },
        { QStringLiteral("target"), QStringLiteral("Target"), false },
    };
    MainWindow window(router, nullptr);
    QSlider* volume = window.findChild<QSlider*>();
    QSpinBox* volumeInput = window.findChild<QSpinBox*>(QStringLiteral("volumeInput"));
    expect(volume != nullptr, "volume slider is discoverable for tests");
    expect(volumeInput != nullptr, "volume input is discoverable for tests");
    if (!volume || !volumeInput)
        return;

    expect(volume->minimum() == 0 && volume->maximum() == 500,
           "volume slider spans 0 to 500 percent");
    expect(volumeInput->minimum() == 0 && volumeInput->maximum() == 500,
           "volume input spans 0 to 500 percent");
    volume->setValue(500);
    expect(std::abs(router->lastVolume - 5.0f) < 0.0001f,
           "maximum volume maps to a 5x gain");
    volume->setValue(95);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(volume, &release);
    expect(volumeInput->value() == 100,
           "slider snaps nearby values to the 100 percent preset");
    volumeInput->setValue(437);
    expect(volumeInput->value() == 437 && std::abs(router->lastVolume - 4.37f) < 0.0001f,
           "volume input updates the slider without snapping exact values");
    volumeInput->setValue(100);
    const int positionAt100 = volume->value();
    volumeInput->setValue(50);
    const int positionAt50 = volume->value();
    volumeInput->setValue(150);
    const int positionAt150 = volume->value();
    volumeInput->setValue(200);
    const int positionAt200 = volume->value();
    expect(positionAt50 == 50 && positionAt100 - positionAt50 == 50
               && positionAt150 - positionAt100 == 50
               && positionAt200 - positionAt150 == 50,
           "volume slider keeps 0 to 200 uniformly spaced");
    volumeInput->setValue(300);
    const int positionAt300 = volume->value();
    volumeInput->setValue(500);
    const int positionAt500 = volume->value();
    expect(positionAt300 - positionAt200 > positionAt500 - positionAt300,
           "volume slider packs larger values more densely");
}

void testLanguageSwitchUpdatesWidgetsAndTray()
{
    setLanguageSetting(QStringLiteral("en"));
    clearSavedDeviceSettings();

    auto* router = new FakeRouter;
    router->devices = {
        { QStringLiteral("source-a"), QStringLiteral("Source A"), true },
        { QStringLiteral("target-b"), QStringLiteral("Target B"), false },
    };
    MainWindow window(router, nullptr);
    QComboBox* source = sourceBox(window);
    QMenu* menu = languageMenu(window, QStringLiteral("Language"));
    expect(window.windowTitle() == QStringLiteral("Audio Monitor"),
           "English is applied from the explicit setting");
    expect(hasButtonText(window, QStringLiteral("Refresh devices")),
           "English button text is visible");
    expect(hasLabelText(window, QStringLiteral("Listen source (output device):")),
           "English label text is visible");
    expect(menu != nullptr, "English language menu is discoverable");
    expect(windowAction(window, QStringLiteral("Open main window")) != nullptr,
           "English tray action text is visible");
    expect(source && source->itemText(0) == QStringLiteral("Source A (default)"),
           "English default-device suffix is visible");

    expect(invoke(window, "selectSimplifiedChinese"),
           "Simplified Chinese slot is invokable");
    const QString translatedRefresh = QCoreApplication::translate("MainWindow", "Refresh devices");
    const QString translatedSourceTitle = QCoreApplication::translate(
        "MainWindow", "Listen source (output device):");
    const QString translatedOpen = QCoreApplication::translate("MainWindow", "Open main window");
    const QString translatedLanguage = QCoreApplication::translate("MainWindow", "Language");
    const QString translatedChinese = QCoreApplication::translate("MainWindow", "Simplified Chinese");
    const QString translatedDefault = QCoreApplication::translate("MainWindow", " (default)");
    menu = languageMenu(window, translatedLanguage);
    expect(window.windowTitle() == QStringLiteral("Audio Monitor"),
           "window title stays constant after switching to Simplified Chinese");
    expect(hasButtonText(window, translatedRefresh),
           "button text changes immediately to Simplified Chinese");
    expect(hasLabelText(window, translatedSourceTitle),
           "label text changes immediately to Simplified Chinese");
    expect(translatedOpen != QStringLiteral("Open main window")
               && windowAction(window, translatedOpen) != nullptr,
           "tray action text changes immediately to Simplified Chinese");
    expect(menu != nullptr, "Simplified Chinese language menu is discoverable");
    QAction* english = menuAction(menu, QStringLiteral("English"));
    QAction* chinese = menuAction(menu, translatedChinese);
    expect(english && chinese, "translated language actions are present");
    expect(english && !english->isChecked() && chinese && chinese->isChecked(),
           "language action check state follows the active language");
    expect(source && source->itemText(0) == QStringLiteral("Source A") + translatedDefault,
           "default suffix changes without translating the device name");
    expect(source && source->itemText(0).contains(QStringLiteral("Source A")),
           "operating-system device names remain unchanged");
}

void testLanguagePersistenceAndLocaleDefault()
{
    setLanguageSetting(QStringLiteral("en"));
    clearSavedDeviceSettings();

    {
        auto* router = new FakeRouter;
        MainWindow window(router, nullptr);
        expect(invoke(window, "selectSimplifiedChinese"),
               "manual language selection is invokable");
        QSettings settings;
        expect(settings.value(QStringLiteral("ui/language")).toString()
                   == QStringLiteral("zh_CN"),
               "manual Simplified Chinese selection is persisted");
    }

    clearSavedDeviceSettings();
    {
        auto* router = new FakeRouter;
        MainWindow window(router, nullptr);
        expect(window.windowTitle() == QStringLiteral("Audio Monitor"),
               "persisted Simplified Chinese keeps the constant window title");
        expect(invoke(window, "selectEnglish"), "English slot is invokable");
        QSettings settings;
        expect(settings.value(QStringLiteral("ui/language")).toString()
                   == QStringLiteral("en"),
               "manual English selection is persisted");
    }

    setLanguageSetting(QString());
    clearSavedDeviceSettings();
    {
        auto* router = new FakeRouter;
        MainWindow window(router, nullptr);
        expect(window.windowTitle() == QStringLiteral("Audio Monitor"),
               "missing language setting keeps the constant window title");
    }

    setLanguageSetting(QStringLiteral("en"));
}

void testDeviceNamesRemainOpaque()
{
    setLanguageSetting(QStringLiteral("en"));
    clearSavedDeviceSettings();

    {
        auto* router = new FakeRouter;
        router->devices = {
            { QStringLiteral("source-a"), QStringLiteral("Studio (default)"), true },
            { QStringLiteral("target-b"), QStringLiteral("Target B"), false },
        };
        MainWindow window(router, nullptr);
        QComboBox* source = sourceBox(window);
        expect(source && source->itemText(0) == QStringLiteral("Studio (default) (default)"),
               "a device name ending in the suffix is kept verbatim in English");
        expect(invoke(window, "selectSimplifiedChinese"),
               "opaque device-name language switch is invokable");
        const QString translatedSuffix = QCoreApplication::translate("MainWindow", " (default)");
        expect(source && source->itemText(0) == QStringLiteral("Studio (default)") + translatedSuffix,
               "language switching changes only the generated default marker");
    }

    QSettings settings;
    expect(settings.value(QStringLiteral("source/name")).toString()
               == QStringLiteral("Studio (default)"),
           "settings persist the raw device name without a translated suffix");
}

} // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AudioMonitorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MainWindowTests"));

    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) {
        std::cerr << "Unable to create temporary settings directory\n";
        return EXIT_FAILURE;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    {
        QSettings settings;
        settings.setValue(QStringLiteral("ui/language"), QStringLiteral("en"));
    }

    testRefreshCannotOverwriteSessionIds();
    testRepeatedStartStop();
    testInvalidAndIdenticalSelections();
    testVolumeRange();
    testLanguageSwitchUpdatesWidgetsAndTray();
    testLanguagePersistenceAndLocaleDefault();
    testDeviceNamesRemainOpaque();

    if (failures != 0) {
        std::cerr << failures << " MainWindow assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "MainWindow tests passed\n";
    return EXIT_SUCCESS;
}
