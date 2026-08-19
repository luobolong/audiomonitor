#include "appicon.h"

#include <QIcon>
#include <QSize>
#include <QString>

namespace {
constexpr int kAppIconSizes[] = {16, 24, 32, 48, 64, 128, 256};
}

QIcon makeAppIcon()
{
    QIcon icon;
    for (const int size : kAppIconSizes) {
        const QString path = QStringLiteral(":/appicon_%1.png").arg(size);
        icon.addFile(path, QSize(size, size));
    }
    return icon;
}
