/*
 * AppImage Tools
 * Copyright 2024-2026, Jonas Kvinge <jonas@jkvinge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

#include "appdir.h"
#include "dependencywalker.h"
#include "utilities.h"
#include "qtdeploy.h"
#include "qtdeployinfo.h"

using namespace Qt::Literals::StringLiterals;

namespace {

bool AnyElfEndsWith(const DependencyWalker &dependency_walker, const QString &suffix) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (lib.endsWith(suffix)) return true;
  }

  return false;

}

QString QueryQmake(const QString &qmake_path, const QString &var) {

  QProcess process;
  process.start(qmake_path, {u"-query"_s, var});
  if (!process.waitForFinished(-1) || process.exitCode() != 0) return QString();
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();

}

// Asks qmake directly where Qt's plugins live, via the same mechanism every other Qt deployment tool (windeployqt, macdeployqt, linuxdeployqt) relies on for this.
// Far more reliable than reading the qt_prfxpath string baked into libQt*Core: that string is only meaningful for genuinely relocatable Qt builds, and is frequently a short relative value like ".." on distro-packaged, non-relocatable Qt (the library is never going to move, so there was nothing useful to bake in) - a value that isn't usable without knowing what it's relative to, which the ELF alone doesn't tell us.
//
// `core_lib` is the absolute path of the libQt*Core.so.* that's actually bundled (see the caller for why that specific file, not a fresh independent search); the first qmake candidate found on $PATH is only trusted if its own QT_INSTALL_LIBS matches that file's directory - otherwise it's simply answering for some *other* Qt install (e.g. the system one, when the app was actually built against a custom Qt not on $PATH), and trusting it would bundle plugins for the wrong Qt entirely.
// Returns an empty string if no candidate qmake's answer can be verified to match.
QString QueryQmakeInstallPlugins(const QString &core_lib) {

  const QString core_lib_dir = QDir::cleanPath(QFileInfo(core_lib).absolutePath());

  for (const QString &qmake_name : {u"qmake%1"_s.arg(QtDeploy::kQtVersion), u"qmake"_s}) {
    const QString qmake_path = QStandardPaths::findExecutable(qmake_name);
    if (qmake_path.isEmpty()) continue;

    const QString install_libs = QueryQmake(qmake_path, u"QT_INSTALL_LIBS"_s);
    if (install_libs.isEmpty() || QDir::cleanPath(install_libs) != core_lib_dir) {
      qInfo() << "Ignoring" << qmake_path << "- its QT_INSTALL_LIBS" << install_libs << "does not match the bundled" << core_lib;
      continue;
    }

    const QString plugins = QueryQmake(qmake_path, u"QT_INSTALL_PLUGINS"_s);
    if (!plugins.isEmpty() && QDir(plugins).exists()) return plugins;
  }

  return QString();

}

bool GetQtPrefixPath(DependencyWalker &dependency_walker, QString &prefix_path, bool &quirks_mode, QString &error_message) {

  QString qt_dir = qEnvironmentVariable("QTDIR");
  if (qt_dir.isEmpty()) qt_dir = qEnvironmentVariable("QT_ROOT_DIR");

  // Deliberately looked up in all_elfs() (the actual, already-resolved set of bundled ELFs) rather than via a fresh FindLibrary() call: by this point library_locations_ has accumulated rpath directories from every dependency processed during the walk, not just the main executable's own, so a second independent FindLibrary("libQt6Core.so.6") lookup can resolve to a different (wrong) copy than the one that actually got bundled as the app's real Qt dependency - e.g. if some unrelated bundled library's rpath happens to also point at another Qt install.
  const QString core_lib_name = u"libQt%1Core.so.%1"_s.arg(QtDeploy::kQtVersion);
  QString core_lib;
  for (const QString &lib : dependency_walker.all_elfs()) {
    if (QFileInfo(lib).fileName() == core_lib_name) {
      core_lib = lib;
      break;
    }
  }
  if (core_lib.isEmpty()) core_lib = dependency_walker.FindLibrary(core_lib_name);

  QString prfxpath;
  if (!qt_dir.isEmpty()) {
    qInfo() << "Using QTDIR or QT_ROOT_DIR:" << qt_dir;
    prfxpath = qt_dir;
  }
  else if (const QString qmake_plugins_dir = core_lib.isEmpty() ? QString() : QueryQmakeInstallPlugins(core_lib); !qmake_plugins_dir.isEmpty()) {
    qInfo() << "Using qmake -query QT_INSTALL_PLUGINS:" << qmake_plugins_dir;
    prfxpath = QFileInfo(qmake_plugins_dir).absolutePath();  // parent of ".../plugins" is the prefix.
  }
  else {
    if (core_lib.isEmpty()) {
      error_message = u"Could not find %1"_s.arg(core_lib_name);
      return false;
    }
    prfxpath = Utilities::ReadNullTerminatedStringAfter(core_lib, "qt_prfxpath=");
    if (prfxpath.isEmpty()) {
      error_message = u"Could not determine qt_prfxpath from %1"_s.arg(core_lib);
      return false;
    }
    // A relative qt_prfxpath (e.g. "..", common on non-relocatable distro Qt builds, where there was never a need to bake in anything more useful) is meaningless resolved against this process's own working directory - it has to be resolved against the directory the library file itself sits in.
    if (QDir::isRelativePath(prfxpath)) {
      prfxpath = QDir(QFileInfo(core_lib).absolutePath()).absoluteFilePath(prfxpath);
      prfxpath = QDir::cleanPath(prfxpath);
    }
    qInfo() << "qt_prfxpath:" << prfxpath;
  }

  quirks_mode = false;
  if (!QDir(prfxpath + "/plugins"_L1).exists()) {
    qInfo() << "Got qt_prfxpath but it does not contain 'plugins', trying to locate libqxcb.so instead";
    // Scoped to the real Qt library's own directory tree (not this process's working directory, and not the AppDir being assembled, which may still contain stale plugins left over from an earlier, unrelated deploy run) - the only directory we actually know for certain is part of the real Qt install this binary is linked against.
    const QString search_root = core_lib.isEmpty() ? prfxpath : QFileInfo(core_lib).absolutePath();
    const QStringList found = Utilities::FilesWithSuffixRecursive(search_root, u"libqxcb.so"_s);
    if (found.isEmpty()) {
      error_message = u"Could not determine the path to Qt automatically. "
                       u"Please set $QTDIR to the path to Qt (the directory that contains plugins/, qml/, etc.) and try again"_s;
      return false;
    }
    QDir dir(QFileInfo(found.first()).absolutePath());  // .../plugins/platforms
    dir.cdUp();  // .../plugins
    dir.cdUp();  // The Qt prefix.
    prfxpath = dir.absolutePath();
    qInfo() << "Guessed qt_prfxpath to be" << prfxpath;
    quirks_mode = true;
  }

  prefix_path = prfxpath;

  return true;

}

void DeployIfExists(DependencyWalker &dependency_walker, const QString &path) {

  if (QFileInfo::exists(path)) {
    dependency_walker.AddElfTree(path);
  }
  else {
    qInfo() << "Skipping" << path << "because it does not exist";
  }

}

// Maps a driver name (e.g. "sqlite", "mysql", "psql", "odbc", "oci", "db2", "ibase", "mimer") to its plugin filename. Every driver follows "libqsql<name>.so" except sqlite, whose plugin is irregularly named "libqsqlite.so" (no second "sql").
QString SqlDriverPluginFilename(const QString &driver_name) {

  if (driver_name.compare(u"sqlite"_s, Qt::CaseInsensitive) == 0) return u"libqsqlite.so"_s;
  return u"libqsql%1.so"_s.arg(driver_name.toLower());

}

bool DeploySqlDrivers(DependencyWalker &dependency_walker, const QString &prefix_path, QtDeploy::SqlPluginSet sql_plugin_set, const QStringList &sql_plugin_names, QString &error_message) {

  const QString sqldrivers_dir = prefix_path + "/plugins/sqldrivers/"_L1;

  if (sql_plugin_set == QtDeploy::SqlPluginSet::None) {
    qInfo() << "Skipping Qt SQL drivers (--qt-sql-plugins=none)";
    return true;
  }

  if (sql_plugin_set == QtDeploy::SqlPluginSet::All) {
    DeployIfExists(dependency_walker, sqldrivers_dir);
    return true;
  }

  for (const QString &driver_name : sql_plugin_names) {
    const QString plugin_file = sqldrivers_dir + SqlDriverPluginFilename(driver_name);
    if (!QFileInfo::exists(plugin_file)) {
      error_message = u"Could not find Qt SQL driver '%1' (expected at %2)"_s.arg(driver_name, plugin_file);
      return false;
    }
    dependency_walker.AddElfTree(plugin_file);
  }

  qInfo() << "Selected" << sql_plugin_names.size() << "explicitly named Qt SQL drivers";

  return true;

}

void DeployQml(DependencyWalker &dependency_walker, const AppDir &appdir, const QString &qt_prefix_path) {

  const QStringList scanner_candidates = Utilities::FilesWithSuffixRecursive(qt_prefix_path, u"qmlimportscanner"_s);
  QString scanner = scanner_candidates.isEmpty() ? QString() : scanner_candidates.first();
  if (scanner.isEmpty()) scanner = QStandardPaths::findExecutable(u"qmlimportscanner"_s);
  if (scanner.isEmpty()) {
    qInfo() << "qmlimportscanner not found, skipping QML deployment";
    return;
  }
  qInfo() << "Found qmlimportscanner:" << scanner;

  const QString import_path = qt_prefix_path + "/qml"_L1;

  qInfo() << "Deploying QML imports";
  qInfo() << "Application root path is" << appdir.path();
  qInfo() << "QML module search path is" << import_path;

  QProcess process;
  process.start(scanner, {u"-rootPath"_s, appdir.path(), u"-importPath"_s, import_path});
  if (!process.waitForFinished(-1)) {
    qWarning() << "qmlimportscanner did not finish:" << process.errorString();
    return;
  }
  const QByteArray output = process.readAllStandardOutput();
  if (process.exitCode() != 0) {
    qWarning() << "qmlimportscanner exited with code" << process.exitCode() << ":" << process.readAllStandardError();
    return;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(output);
  if (!doc.isArray()) {
    qWarning() << "qmlimportscanner produced unexpected output";
    return;
  }

  const QJsonArray array = doc.array();
  for (const QJsonValue &value : array) {
    const QJsonObject obj = value.toObject();
    if (obj.value("type"_L1).toString() != "module"_L1) continue;

    const QString module_path = obj.value("path"_L1).toString();
    if (module_path.isEmpty()) continue;

    qInfo() << "QML module:" << obj.value("name"_L1).toString() << "at" << module_path;

    const QString target = appdir.path() + Utilities::RemapAppDirPath(module_path, qt_prefix_path);
    QDir().mkpath(QFileInfo(target).absolutePath());
    Utilities::CopyTree(module_path, target);
    qInfo().noquote() << "Copied QML module" << module_path << "->" << target;
    dependency_walker.AddElfTree(target);
  }

}

}  // namespace

namespace QtDeploy {

bool IsQtDependency(const DependencyWalker &dependency_walker) {

  const bool found = AnyElfEndsWith(dependency_walker, u"libQt%1Core.so.%1"_s.arg(kQtVersion));
  if (found) qInfo() << "Detected Qt" << kQtVersion;
  return found;

}

bool Deploy(DependencyWalker &dependency_walker, const AppDir &appdir, QtDeployInfo &info, SqlPluginSet sql_plugin_set, const QStringList &sql_plugin_names, QString &error_message) {

  QString prefix_path;
  bool quirks = false;
  if (!GetQtPrefixPath(dependency_walker, prefix_path, quirks, error_message)) return false;

  info.qt_prefix_path = prefix_path;
  info.quirks_mode_patch_qt_prfxpath = quirks;

  qInfo() << "Looking in" << prefix_path + "/plugins"_L1;

  const QString platforms_plugin = prefix_path + "/plugins/platforms/libqxcb.so"_L1;
  if (!QFileInfo::exists(platforms_plugin)) {
    error_message = u"Could not find 'plugins/platforms/libqxcb.so' in qt_prfxpath (%1)"_s.arg(prefix_path);
    return false;
  }
  dependency_walker.AddElfTree(platforms_plugin);

  qInfo() << "Selecting for deployment required Qt plugins...";

  const QString network = u"libQt%1Network.so.%1"_s.arg(kQtVersion);
  const QString sql = u"libQt%1Sql.so.%1"_s.arg(kQtVersion);
  const QString gui = u"libQt%1Gui.so.%1"_s.arg(kQtVersion);
  const QString xcb_qpa = u"libQt%1XcbQpa.so.%1"_s.arg(kQtVersion);
  const QString opengl = u"libQt%1OpenGL.so.%1"_s.arg(kQtVersion);
  const QString printsupport = u"libQt%1PrintSupport.so.%1"_s.arg(kQtVersion);
  const QString positioning = u"libQt%1Positioning.so.%1"_s.arg(kQtVersion);
  const QString multimedia = u"libQt%1Multimedia.so.%1"_s.arg(kQtVersion);

  if (AnyElfEndsWith(dependency_walker, network)) {
    DeployIfExists(dependency_walker, prefix_path + "/plugins/bearer/"_L1);
    DeployIfExists(dependency_walker, prefix_path + "/plugins/tls/"_L1);

    // Qt 6's TLS plugins require OpenSSL
    QString ssl_lib = dependency_walker.FindLibrary(u"libssl.so.4"_s);
    if (ssl_lib.isEmpty()) {
      ssl_lib = dependency_walker.FindLibrary(u"libssl.so.3"_s);
      if (ssl_lib.isEmpty()) {
        error_message = u"Could not find libssl.so.4 or libssl.so.3, required by Qt %1's TLS plugin"_s.arg(kQtVersion);
        return false;
      }
    }
    dependency_walker.AddElfTree(ssl_lib);
  }

  if (AnyElfEndsWith(dependency_walker, sql)) {
    if (!DeploySqlDrivers(dependency_walker, prefix_path, sql_plugin_set, sql_plugin_names, error_message)) return false;
  }

  if (AnyElfEndsWith(dependency_walker, gui)) {
    DeployIfExists(dependency_walker, prefix_path + "/plugins/iconengines/"_L1);
    DeployIfExists(dependency_walker, prefix_path + "/plugins/imageformats/"_L1);
    DeployIfExists(dependency_walker, prefix_path + "/plugins/platforminputcontexts/"_L1);
  }

  if (AnyElfEndsWith(dependency_walker, gui) || AnyElfEndsWith(dependency_walker, opengl) || AnyElfEndsWith(dependency_walker, xcb_qpa) || AnyElfEndsWith(dependency_walker, u"libxcb-glx.so"_s)) {
    DeployIfExists(dependency_walker, prefix_path + "/plugins/xcbglintegrations/"_L1);
  }

  if (AnyElfEndsWith(dependency_walker, printsupport)) {
    DeployIfExists(dependency_walker, prefix_path + "/plugins/printsupport/libcupsprintersupport.so"_L1);
  }

  if (AnyElfEndsWith(dependency_walker, positioning)) {
    DeployIfExists(dependency_walker, prefix_path + "/plugins/position/"_L1);
  }

  if (AnyElfEndsWith(dependency_walker, multimedia)) {
    // Plugin directory names/architecture changed across Qt Multimedia releases; DeployIfExists() is a silent no-op when a directory doesn't exist on the Qt version in use.
    DeployIfExists(dependency_walker, prefix_path + "/plugins/multimedia/"_L1);
    DeployIfExists(dependency_walker, prefix_path + "/plugins/mediaservice/"_L1);
    DeployIfExists(dependency_walker, prefix_path + "/plugins/audio/"_L1);
  }

  DeployQml(dependency_walker, appdir, prefix_path);

  return true;

}

}  // namespace QtDeploy
