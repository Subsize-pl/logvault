#pragma once

#include <QString>

/// The application's name and version, kept in one place for every part that shows or stores them.
namespace AppInfo {

QString name();

/// The version from the project() call in CMakeLists.txt, e.g. "1.0.0".
QString version();

} // namespace AppInfo
