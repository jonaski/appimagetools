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

#include <optional>

#include <QByteArray>
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include "appimagedeploy.h"
#include "appdir.h"
#include "dependencywalker.h"
#include "elffile.h"
#include "gstreamerdeploy.h"
#include "miscdeploy.h"
#include "qtdeploy.h"
#include "qtdeployinfo.h"
#include "utilities.h"
#include "appimagedeployoptions.h"
#include "excludelist.h"

using std::optional;
using namespace Qt::Literals::StringLiterals;

namespace {

QString BuildAppRunScript(const bool preserve_cwd) {

  Q_UNUSED(preserve_cwd)

  QFile file(u":/apprun.sh"_s);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Could not read embedded AppRun template";
    return QString();
  }
  const QByteArray data = file.readAll();
  const QString content = QString::fromUtf8(data);

  return content;

}

// Returns the PT_INTERP value of the main executable, or an empty string on failure with error_message set.
QString DeployInterpreter(DependencyWalker &dependency_walker, const AppDir &appdir, const AppImageDeployOptions options, QString &error_message) {

  const QString ld_linux = appdir.ElfInterpreter(error_message);
  if (ld_linux.isEmpty()) return QString();

  const QString existing_target = appdir.path() + ld_linux;
  if (QFileInfo::exists(existing_target)) {
    qInfo() << "Removing pre-existing" << ld_linux << "...";
    if (!QFile::remove(existing_target)) {
      error_message = u"Could not remove pre-existing %1"_s.arg(existing_target);
      return QString();
    }
  }

  if (options.testFlag(AppImageDeployOption::UseExcludeList) && ExcludeList::Contains(QFileInfo(ld_linux).fileName())) {
    qInfo() << "Not deploying" << ld_linux << "because it was not requested, or it is not needed";
    return ld_linux;
  }

  QString src = QFileInfo(ld_linux).canonicalFilePath();
  if (src.isEmpty()) src = ld_linux;

  qInfo() << "Deploying" << ld_linux << "...";
  const QString ld_target_path = appdir.path() + ld_linux;
  if (!Utilities::CopyFile(src, ld_target_path)) {
    error_message = u"Could not copy ld-linux to %1"_s.arg(ld_target_path);
    return QString();
  }

  qInfo() << "Patching ld-linux...";
  Utilities::PatchFile(ld_target_path, "/lib", "/XXX");
  Utilities::PatchFile(ld_target_path, "/usr", "/xxx");
  // --inhibit-cache doesn't reliably stop it from still consulting /etc/ld.so.cache.
  Utilities::PatchFile(ld_target_path, "/etc", "/EEE");

  qInfo() << "Determining gconv (for GCONV_PATH)...";
  const QStringList gconvs = dependency_walker.FindWithPrefixInLibraryLocations(u"gconv"_s);
  if (!gconvs.isEmpty()) dependency_walker.AddElfTree(gconvs.first());

  constexpr QFileDevice::Permissions kExecutablePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
  if (!QFile::setPermissions(ld_target_path, kExecutablePermissions)) {
    error_message = u"Could not set permissions on %1"_s.arg(ld_target_path);
    return QString();
  }

  return ld_linux;

}

// Copies `lib` into the AppDir unless it's already there.
void DeployElf(const QString &lib, const AppDir &appdir, const QString &qt_prefix_path) {

  if (lib.startsWith(appdir.path())) return;  // Already in the AppDir.
  if (QFileInfo(lib).isDir()) {
    qInfo() << lib << "is a directory, skipping";
    return;
  }

  const QString target = appdir.path() + Utilities::RemapAppDirPath(lib, qt_prefix_path);
  if (Utilities::CopyFile(lib, target)) {
    qInfo().noquote() << "Copied" << lib << "->" << target;
  }
  else {
    qWarning() << target << "could not be copied";
  }

}

void PatchRpathsInElf(const AppDir &appdir, const QStringList &library_locations_in_appdir, const QString &lib_path, const QString &qt_prefix_path) {

  QString path = lib_path;
  if (!path.startsWith(appdir.path())) path = QDir::cleanPath(appdir.path() + Utilities::RemapAppDirPath(path, qt_prefix_path));

  const QString base_name = QFileInfo(path).fileName();
  if (base_name.startsWith("ld-"_L1) || base_name.startsWith("libc."_L1)) {
    qInfo() << "Not patching rpath because file starts with ld- or libc.";
    return;
  }

  if (!QFileInfo::exists(path)) return;

  QStringList new_rpaths;
  const QDir path_dir(QFileInfo(path).absolutePath());
  for (const QString &loc : library_locations_in_appdir) {
    new_rpaths << "$ORIGIN/"_L1 + QDir::cleanPath(path_dir.relativeFilePath(loc));
  }

  ElfFile elf_file(path);
  if (elf_file.IsValid() && !elf_file.SetRpath(new_rpaths)) qWarning() << "Failed to set rpath on" << path;

}

// Patches the qt_prfxpath string embedded in libQt<QtDeploy::kQtVersion>Core.so.<QtDeploy::kQtVersion> so the bundled Qt finds its own plugins.
void PatchQtPrefixPath(const AppDir &appdir, const QString &lib_path_in, const QStringList &library_locations_in_appdir, const bool quirks_mode, const QString &qt_prefix_path) {

  qInfo() << "Patching qt_prfxpath in" << lib_path_in;

  QString lib_path;
  const QString abs_app_dir = QFileInfo(appdir.path()).absoluteFilePath();
  const QString abs_lib = QFileInfo(lib_path_in).absoluteFilePath();
  if (abs_lib.startsWith(abs_app_dir)) {
    lib_path = abs_lib;
  }
  else if (const QString remapped = abs_app_dir + Utilities::RemapAppDirPath(abs_lib, qt_prefix_path); QFileInfo::exists(remapped)) {
    lib_path = remapped;
  }
  else {
    qWarning() << "Could not determine absolute path of" << lib_path_in;
    return;
  }

  QFile file(lib_path);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Could not open" << lib_path << "for reading";
    return;
  }
  const QByteArray content = file.readAll();
  file.close();

  const QByteArray marker = "qt_prfxpath=";
  const int marker_pos = content.indexOf(marker);
  if (marker_pos < 0) {
    qWarning() << "Could not find qt_prfxpath token in" << lib_path;
    return;
  }
  const int offset = marker_pos + marker.size();
  int end = content.indexOf('\0', offset);
  if (end < 0) end = content.size();
  const int available = end - offset;

  // Computed by stripping the suffix textually rather than via QDir::cdUp(), which requires each intermediate directory to already exist on disk - not guaranteed here, since libQt*Core.so.* is often copied in (and patched) before the plugins/platforms directory has been created by a later iteration of the same loop.
  QString qt_prefix_dir_in_appdir;
  for (const QString &loc : library_locations_in_appdir) {
    static const QString kPluginsPlatformsSuffix = "/plugins/platforms"_L1;
    if (loc.endsWith(kPluginsPlatformsSuffix)) {
      qt_prefix_dir_in_appdir = loc.left(loc.size() - kPluginsPlatformsSuffix.size());
      break;
    }
  }
  if (qt_prefix_dir_in_appdir.isEmpty()) {
    qWarning() << "Could not determine the Qt prefix directory in the AppDir";
    return;
  }

  // Qt resolves a relative qt_prfxpath against the main executable's own directory (argv[0]/applicationDirPath), not against the ELF interpreter's directory - this holds whether AppRun ends up exec'ing the binary directly or via an explicitly-invoked ld-linux, since the latter re-execs so the target binary still sees itself as argv[0].
  const QString main_executable_dir_in_appdir = QFileInfo(appdir.main_executable()).absolutePath();
  const QString rel_path_to_qt = QDir(main_executable_dir_in_appdir).relativeFilePath(qt_prefix_dir_in_appdir);

  const QByteArray new_value = quirks_mode ? QByteArray("..") : rel_path_to_qt.toUtf8();
  if (new_value.size() > available) {
    qWarning() << "New qt_prfxpath value" << new_value << "does not fit in the space reserved in" << lib_path << "(this Qt build may not support relocation) - leaving untouched";
    return;
  }

  QFile fw(lib_path);
  if (!fw.open(QIODevice::ReadWrite)) {
    qWarning() << "Could not open" << lib_path << "for writing";
    return;
  }
  fw.seek(offset);
  fw.write(new_value);
  fw.write("\x00", 1);

}

}  // namespace

namespace AppImageDeploy {

bool Run(const QString &desktop_file_path, const AppImageDeployOptions &options, GStreamerDeploy::PluginSet gstreamer_plugin_set, const QStringList &gstreamer_plugin_names, QtDeploy::SqlPluginSet qt_sql_plugin_set, const QStringList &qt_sql_plugin_names, QString &error_message) {

  const optional<AppDir> appdir_opt = AppDir::Create(desktop_file_path, error_message);
  if (!appdir_opt) return false;

  const AppDir appdir = appdir_opt.value();

  DependencyWalker dependency_walker(options);

  qInfo() << "Gathering all required libraries for the AppDir...";
  // Resolved via its own rpath/runpath first and foremost, before the general tree walk below sees whatever else already happens to be sitting under the prefix (plugins, helper tools, or - on a prefix reused across deploys of a differently-linked build - stale same-named leftovers from an earlier run) - see AddPriorityElf()'s doc comment.
  dependency_walker.AddPriorityElf(appdir.main_executable());
  dependency_walker.AddElfTree(appdir.path());

  if (!MiscDeploy::HandleGdk(dependency_walker, appdir, error_message)) return false;
  if (!GStreamerDeploy::Deploy(dependency_walker, gstreamer_plugin_set, gstreamer_plugin_names, error_message)) return false;
  if (!MiscDeploy::DeployGtkDirectory(dependency_walker, appdir, 4, error_message)) return false;
  if (!MiscDeploy::DeployGtkDirectory(dependency_walker, appdir, 3, error_message)) return false;
  if (!MiscDeploy::DeployGtkDirectory(dependency_walker, appdir, 2, error_message)) return false;

  MiscDeploy::HandleAlsa(dependency_walker);
  MiscDeploy::HandlePulseAudio(dependency_walker);
  MiscDeploy::HandleGioModules(dependency_walker, appdir);
  MiscDeploy::HandleGnuTls(dependency_walker, appdir);

  const QString ld_linux = DeployInterpreter(dependency_walker, appdir, options, error_message);
  if (ld_linux.isEmpty() && !error_message.isEmpty()) return false;

  if (QDir(appdir.path() + "/usr/share/glib-2.0/schemas"_L1).exists()) {
    if (!MiscDeploy::HandleGlibSchemas(appdir, error_message)) return false;
  }

  if (!MiscDeploy::DeployFontconfig(appdir, error_message)) return false;

  qInfo() << "Adding AppRun...";
  const QString app_run_path = appdir.path() + "/AppRun"_L1;
  QFile app_run_file(app_run_path);
  if (!app_run_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    error_message = u"Could not write %1"_s.arg(app_run_path);
    return false;
  }
  app_run_file.write(BuildAppRunScript(options.testAnyFlag(AppImageDeployOption::PreserveCwd)).toUtf8());
  app_run_file.close();

  constexpr QFileDevice::Permissions kExecutablePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
  QFile::setPermissions(app_run_path, kExecutablePermissions);

  // Deployed alongside AppRun so end users can get verbose Qt category logging out of an already-built AppImage (via APPIMAGE_QT_DEBUG=1, see AppRun) without needing a rebuild.
  const QString qtlogging_ini_path = appdir.path() + "/qtlogging.ini"_L1;
  if (QFileInfo::exists(qtlogging_ini_path)) QFile::remove(qtlogging_ini_path);
  if (!QFile::copy(u":/qtlogging.ini"_s, qtlogging_ini_path)) {
    qWarning() << "Could not write" << qtlogging_ini_path;
  }

  qInfo() << "Find out whether Qt is a dependency of the application to be bundled...";
  const bool has_qt = QtDeploy::IsQtDependency(dependency_walker);
  QtDeployInfo qt_info;
  if (has_qt) {
    if (!QtDeploy::Deploy(dependency_walker, appdir, qt_info, qt_sql_plugin_set, qt_sql_plugin_names, error_message)) return false;
  }

  qInfo() << "";
  qInfo() << "library_locations:";
  for (const QString &lib : dependency_walker.library_locations()) {
    qInfo().noquote() << lib;
  }

  // This is used when calculating the rpath written into every ELF as it's copied into the AppDir, and when patching the rpath of ELFs that were already in the AppDir.
  QStringList library_locations_in_appdir;
  for (QString loc : dependency_walker.library_locations()) {
    if (!loc.startsWith(appdir.path())) loc = appdir.path() + Utilities::RemapAppDirPath(loc, qt_info.qt_prefix_path);
    if (!library_locations_in_appdir.contains(loc)) library_locations_in_appdir << loc;
  }

  qInfo() << "Only after this point should we start copying around any ELFs";
  qInfo() << "Copying in and patching ELFs which are not already in the AppDir...";

  if (!MiscDeploy::HandleNvidia(dependency_walker, error_message)) return false;

  const QString qt_core_lib_name = has_qt ? u"libQt%1Core.so.%1"_s.arg(QtDeploy::kQtVersion) : QString();

  for (const QString &lib : dependency_walker.all_elfs()) {
    DeployElf(lib, appdir, qt_info.qt_prefix_path);
    PatchRpathsInElf(appdir, library_locations_in_appdir, lib, qt_info.qt_prefix_path);

    if (!qt_core_lib_name.isEmpty() && lib.contains(qt_core_lib_name)) {
      PatchQtPrefixPath(appdir, lib, library_locations_in_appdir, qt_info.quirks_mode_patch_qt_prfxpath, qt_info.qt_prefix_path);
    }
  }

  if (!options.testFlag(AppImageDeployOption::UseExcludeList)) {
    qInfo() << "To check whether it is really self-contained, run:";
    qInfo().noquote() << "LD_LIBRARY_PATH='' find "_L1 + appdir.path() + " -type f -exec ldd {} 2>&1 \\; | grep '=>' | grep -v "_L1 + appdir.path();
  }

  return true;

}

}  // namespace AppImageDeploy
