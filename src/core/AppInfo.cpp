#include "AppInfo.h"

namespace AppInfo {

QString name() { return QStringLiteral("LogVault"); }

// LV_VERSION is a compile definition private to lv_core (see src/core/CMakeLists.txt).
QString version() { return QStringLiteral(LV_VERSION); }

} // namespace AppInfo
