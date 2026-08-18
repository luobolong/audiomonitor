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

// --list-devices：打印可用输出设备后退出（便于脚本与调试）。
int listDevicesMode(QCoreApplication& app)
{
    const std::unique_ptr<AudioRouter> router(AudioRouter::create());
    QTextStream out(stdout);
    const QVector<DeviceInfo> devices = router->outputDevices();
    for (const DeviceInfo& d : devices)
        out << d.id << '\t' << d.name << (d.isDefault ? "\t(default)" : "") << '\n';
    out.flush();
    return devices.isEmpty() ? 1 : 0;
}

// --forward <sourceId> <targetId> [volume]：无界面直接转发（调试/测试用），
// 持续运行直到被终止（Ctrl+C / kill）。
// 可选 --dump-capture <path>：把捕获到的原始音频导出到文件。
int forwardMode(QCoreApplication& app, const QStringList& args)
{
    QTextStream out(stdout);
    const std::unique_ptr<AudioRouter> router(AudioRouter::create());
    QObject::connect(router.get(), &AudioRouter::started, [&]() {
        out << "转发已启动" << '\n';
        out.flush();
    });
    QObject::connect(router.get(), &AudioRouter::stopped, [&]() {
        out << "转发已停止" << '\n';
        out.flush();
    });
    QObject::connect(router.get(), &AudioRouter::errorOccurred, [&](const QString& msg) {
        out << "错误: " << msg << '\n';
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
    // GUI 线程为 STA：WASAPI 设备枚举/通知回调要求 COM 初始化
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AudioMonitor"));
    app.setApplicationDisplayName(QStringLiteral("音频监听转发"));
    app.setOrganizationName(QStringLiteral("AudioMonitor"));
    app.setQuitOnLastWindowClosed(false); // 关闭窗口后继续在托盘运行
    app.setWindowIcon(makeAppIcon());

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--list-devices")))
        return listDevicesMode(app);
    if (args.contains(QStringLiteral("--forward"))) {
        const int idx = args.indexOf(QStringLiteral("--forward"));
        if (idx + 2 >= args.size()) {
            QTextStream(stderr) << "用法: audiomonitor --forward <sourceId> <targetId> [volume]"
                                << '\n';
            return 2;
        }
        return forwardMode(app, args.mid(idx));
    }

    MainWindow w;
    w.show();

    // --smoke-test：N 毫秒后自动退出（CI/冒烟测试用）
    const int smokeIdx = args.indexOf(QStringLiteral("--smoke-test"));
    if (smokeIdx >= 0 && smokeIdx + 1 < args.size()) {
        bool ok = false;
        const int ms = args.at(smokeIdx + 1).toInt(&ok);
        if (ok && ms > 0)
            QTimer::singleShot(ms, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
