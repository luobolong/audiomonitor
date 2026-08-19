#pragma once

#include <QIcon>

// Build the application icon from the binary PNG assets embedded via the Qt
// resource system (resources/resources.qrc). One PNG per common icon size is
// shipped so the tray, window, and taskbar get a crisp image at every scale.
QIcon makeAppIcon();
