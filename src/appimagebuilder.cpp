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

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

#include "appimagebuilder.h"
#include "desktopentry.h"
#include "utilities.h"

using namespace Qt::Literals::StringLiterals;

namespace AppImageBuilder {

bool Build(const QString &app_dir_path, const Options &options, QString &output_path, QString &error_message) {

  if (!QFileInfo::exists(app_dir_path)) {
    error_message = u"The specified directory does not exist"_s;
    return false;
  }
  if (!QFileInfo::exists(app_dir_path + "/AppRun"_L1)) {
    error_message = u"AppRun is missing"_s;
    return false;
  }

  const QStringList desktop_files = Utilities::FilesWithSuffix(app_dir_path, u".desktop"_s);
  if (desktop_files.size() < 1) {
    error_message = u"No top-level desktop file found in %1"_s.arg(app_dir_path);
    return false;
  }
  if (desktop_files.size() > 1) {
    error_message = u"Multiple top-level desktop files found in %1"_s.arg(app_dir_path);
    return false;
  }
  const QString desktop_file = desktop_files.first();

  if (!QStandardPaths::findExecutable(u"desktop-file-validate"_s).isEmpty()) {
    QProcess validate;
    validate.start(u"desktop-file-validate"_s, {desktop_file});
    if (!validate.waitForFinished(-1) || validate.exitCode() != 0) {
      error_message = u"Desktop file contains errors:\n%1"_s.arg(QString::fromUtf8(validate.readAllStandardOutput() + validate.readAllStandardError()));
      return false;
    }
  }

  DesktopEntry desktop_entry;
  if (!desktop_entry.Load(desktop_file)) {
    error_message = u"Could not parse %1"_s.arg(desktop_file);
    return false;
  }
  const QString validation_error = desktop_entry.Validate();
  if (!validation_error.isEmpty()) {
    error_message = validation_error;
    return false;
  }

  QString name = desktop_entry.Value(u"Name"_s);
  const QString name_with_underscores = name.replace(u' ', u'_');
  const QString icon_name = desktop_entry.Value(u"Icon"_s);

  const QString arch = Utilities::DetectArchitecture(app_dir_path, error_message);
  if (arch.isEmpty()) return false;

  QString version = options.version;
  if (version.isEmpty()) {
    const QString exec_value = desktop_entry.Value(u"Exec"_s);
    const QString exec_first_token = exec_value.section(u' ', 0, 0);
    const QString main_executable = app_dir_path + "/usr/bin/"_L1 + exec_first_token;
    if (exec_first_token.isEmpty() || !QFileInfo::exists(main_executable)) {
      error_message = u"Version not found, aborting. Set it with --version"_s;
      return false;
    }
    version = Utilities::DetectVersionFromExecutable(main_executable, error_message);
    if (version.isEmpty()) return false;
  }
  desktop_entry.SetKey(u"X-AppImage-Version"_s, version);
  desktop_entry.Save();

  QString target = options.destination;
  if (target.isEmpty()) {
    target = name_with_underscores + "-"_L1 + version + "-Linux-"_L1 + arch + ".AppImage"_L1;
  }
  else if (QFileInfo(target).isDir()) {
    target = QDir(target).filePath(name_with_underscores + "-"_L1 + version + "-Linux-"_L1 + arch + ".AppImage"_L1);
  }
  qInfo() << "Target AppImage filename:" << target;

  // Prefer a .png (thumbnails need png anyway); fall back to .svg/.xpm.
  struct IconCandidate {
    QString suffix;
    QString warning;
  };
  static const QList<IconCandidate> kSupportedIconExtensions = {
    {u".png"_s, QString()},
    {u".svg"_s, u"SVG support is optional"_s},
    {u".xpm"_s, u"XPM icons are deprecated"_s},
  };
  const QStringList icon_locations = {
    app_dir_path + "/"_L1,
    app_dir_path + "/usr/share/icons/hicolor/256x256/apps/"_L1,
    app_dir_path + "/usr/share/icons/hicolor/scalable/apps/"_L1,
  };

  QString icon_file;
  for (const IconCandidate &candidate : kSupportedIconExtensions) {
    for (const QString &location : icon_locations) {
      const QString candidate_path = location + icon_name + candidate.suffix;
      if (QFileInfo::exists(candidate_path)) {
        icon_file = candidate_path;
        break;
      }
    }
    if (!icon_file.isEmpty()) {
      if (!candidate.warning.isEmpty()) qWarning() << "Icon file" << icon_file << "was found but" << candidate.warning;
      break;
    }
  }
  if (icon_file.isEmpty()) {
    error_message = u"Could not find icon file %1{.png,.svg,.xpm} in any of the usual locations"_s.arg(icon_name);
    return false;
  }
  qInfo() << "Icon file:" << icon_file;

  const QString dir_icon = app_dir_path + "/.DirIcon"_L1;
  if (QFileInfo::exists(dir_icon)) QFile::remove(dir_icon);
  if (icon_file.endsWith(".svg"_L1)) {
    if (!Utilities::ConvertSvgToPng(icon_file, dir_icon, 256)) Utilities::CopyFile(icon_file, dir_icon);
  }
  else {
    Utilities::CopyFile(icon_file, dir_icon);
  }

  const QString appstream_file = app_dir_path + "/usr/share/metainfo/"_L1 + QFileInfo(desktop_file).completeBaseName() + ".appdata.xml"_L1;
  if (!options.check_appstream_metadata) {
    qWarning() << "Skipping AppStream metadata check...";
  }
  else if (!QFileInfo::exists(appstream_file)) {
    qWarning() << "AppStream upstream metadata is missing, please consider creating it at" << appstream_file;
  }
  else {
    if (QStandardPaths::findExecutable(u"appstreamcli"_s).isEmpty()) {
      error_message = u"Required helper tool appstreamcli is missing"_s;
      return false;
    }
    QProcess validate;
    validate.start(u"appstreamcli"_s, {u"validate-tree"_s, app_dir_path, u"--no-net"_s});
    if (!validate.waitForFinished(-1) || validate.exitCode() != 0) {
      error_message = u"AppStream metainfo validation failed:\n%1"_s.arg(QString::fromUtf8(validate.readAllStandardOutput() + validate.readAllStandardError()));
      return false;
    }
  }

  QString runtime_file = options.runtime_file;
  if (runtime_file.isEmpty()) {
    QString runtime_dir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../share/AppImageKit/runtime/"_L1);
    if (!QDir(runtime_dir).exists()) runtime_dir = QCoreApplication::applicationDirPath();
    runtime_file = runtime_dir + "/runtime-"_L1 + arch;
  }
  if (!QFileInfo::exists(runtime_file)) {
    error_message = u"Cannot find %1. It should have been bundled, but you can get it from https://github.com/AppImage/type2-runtime/releases/tag/continuous"_s.arg(runtime_file);
    return false;
  }

  const qint64 offset = QFileInfo(runtime_file).size();
  const qint64 fstime = QDateTime::currentSecsSinceEpoch();

  const QFileDevice::Permissions perms = QFileInfo(app_dir_path).permissions();
  if (!(perms & QFileDevice::ReadOther)) {
    error_message = u"Wrong permissions on %1, please set it to 0755 (world-readable) and try again"_s.arg(app_dir_path);
    return false;
  }

  QProcess process_mksquashfs;
  process_mksquashfs.setProcessChannelMode(QProcess::ForwardedChannels);
  process_mksquashfs.start(u"mksquashfs"_s, {app_dir_path, target, u"-offset"_s, QString::number(offset), u"-fstime"_s, QString::number(fstime), u"-comp"_s, options.squashfs_compression_type, u"-root-owned"_s, u"-noappend"_s, u"-b"_s, u"1M"_s});
  if (!process_mksquashfs.waitForFinished(-1) || process_mksquashfs.exitCode() != 0) {
    error_message = u"mksquashfs failed"_s;
    return false;
  }

  qInfo() << "Embedding runtime...";
  if (!Utilities::WriteFileIntoOtherFileAtOffset(runtime_file, target, 0)) {
    error_message = u"Could not embed the runtime into %1"_s.arg(target);
    return false;
  }

  constexpr QFileDevice::Permissions kExecutablePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
  QFile::setPermissions(target, kExecutablePermissions);

  output_path = QFileInfo(target).absoluteFilePath();

  qInfo() << "Success";
  qInfo() << "The AppImage was created at:" << QFileInfo(target).absoluteFilePath();

  return true;

}

}  // namespace AppImageBuilder
