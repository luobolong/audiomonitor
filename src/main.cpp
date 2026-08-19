#include <QApplication>
#include <QTextStream>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include <objbase.h>
#endif

#include "appicon.h"
#include "core/audiorouter.h"
#include "mainwindow.h"

namespace {

// --list-devices: print available output devices and exit for scripts/debugging.
int listDevicesMode(QCoreApplication& app)
{
    Q_UNUSED(app);
    const std::unique_ptr<AudioRouter> router(AudioRouter::create());
    QTextStream out(stdout);
    const QVector<DeviceInfo> devices = router->outputDevices();
    for (const DeviceInfo& d : devices)
        out << d.id << '\t' << d.name << (d.isDefault ? "\t(default)" : "") << '\n';
    out.flush();
    return devices.isEmpty() ? 1 : 0;
}

// --forward <sourceId> <targetId> [volume]: forward without a GUI for
// debugging/testing until terminated (Ctrl+C/kill).
// Optional dump flags export raw audio or callback information.
int forwardMode(QCoreApplication& app, const QStringList& args)
{
    QTextStream out(stdout);
    const std::unique_ptr<AudioRouter> router(AudioRouter::create());
    QObject::connect(router.get(), &AudioRouter::started, [&]() {
        out << "Forwarding started" << '\n';
        out.flush();
    });
    QObject::connect(router.get(), &AudioRouter::stopped, [&]() {
        out << "Forwarding stopped" << '\n';
        out.flush();
    });
    QObject::connect(router.get(), &AudioRouter::errorOccurred, [&](const QString& msg) {
        out << "Error: " << msg << '\n';
        out.flush();
        QCoreApplication::exit(1);
    });
    const int dumpIdx = args.indexOf(QStringLiteral("--dump-capture"));
    if (dumpIdx >= 0 && dumpIdx + 1 < args.size())
        router->setCaptureDumpFile(args.at(dumpIdx + 1));
    const int pdumpIdx = args.indexOf(QStringLiteral("--dump-playback"));
    if (pdumpIdx >= 0 && pdumpIdx + 1 < args.size())
        router->setPlaybackDumpFile(args.at(pdumpIdx + 1));
    const int cbdumpIdx = args.indexOf(QStringLiteral("--dump-callbacks"));
    if (cbdumpIdx >= 0 && cbdumpIdx + 1 < args.size())
        router->setCallbackDumpFile(args.at(cbdumpIdx + 1));
    const float volume = (args.size() > 3 && !args.at(3).startsWith(QLatin1Char('-')))
                             ? args.at(3).toFloat()
                             : 1.0f;
    if (!router->start(args.at(1), args.at(2), volume))
        return 1;
    return app.exec();
}

} // namespace

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef Q_OS_WIN
    // The GUI thread is an STA because WASAPI enumeration/notifications require COM.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AudioMonitor"));
    app.setOrganizationName(QStringLiteral("AudioMonitor"));
    app.setQuitOnLastWindowClosed(false); // Keep running in the tray after the window closes.
    app.setWindowIcon(makeAppIcon());

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--list-devices")))
        return listDevicesMode(app);
    if (args.contains(QStringLiteral("--forward"))) {
        const int idx = args.indexOf(QStringLiteral("--forward"));
        if (idx + 2 >= args.size()) {
            QTextStream(stderr) << "Usage: audiomonitor --forward <sourceId> <targetId> [volume]"
                                << '\n';
            return 2;
        }
        return forwardMode(app, args.mid(idx));
    }

    MainWindow w;
    w.show();

    // --smoke-test: exit after N milliseconds for CI/smoke tests.
    const int smokeIdx = args.indexOf(QStringLiteral("--smoke-test"));
    if (smokeIdx >= 0 && smokeIdx + 1 < args.size()) {
        bool ok = false;
        const int ms = args.at(smokeIdx + 1).toInt(&ok);
        if (ok && ms > 0)
            QTimer::singleShot(ms, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
