#include "audiorouter.h"

#ifdef Q_OS_WIN
#include "audiorouter_win.h"
#elif defined(Q_OS_LINUX)
#include "audiorouter_linux.h"
#else
#error "AudioMonitor 仅支持 Windows 与 Linux"
#endif

AudioRouter* AudioRouter::create(QObject* parent)
{
#ifdef Q_OS_WIN
    return new AudioRouterWin(parent);
#elif defined(Q_OS_LINUX)
    return new AudioRouterLinux(parent);
#endif
}
