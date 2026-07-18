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

#include <QList>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#include "appdir.h"
#include "desktopentry.h"
#include "elffile.h"
#include "utilities.h"

using std::optional;
using std::nullopt;
using namespace Qt::Literals::StringLiterals;

optional<AppDir> AppDir::Create(const QString &desktop_file_path, QString &error_message) {

  if (!QFileInfo::exists(desktop_file_path)) {
    error_message = u"Desktop file not found"_s;
    return nullopt;
  }

  const QString app_dir_root = Utilities::DirUp(desktop_file_path, 4);
  if (!QFileInfo::exists(app_dir_root + "/usr/bin"_L1)) {
    error_message = u"AppDir could not be identified: %1/usr/bin does not exist"_s.arg(app_dir_root);
    return nullopt;
  }

  AppDir appdir;
  appdir.path_ = app_dir_root;

  qInfo() << "AppDir path:" << appdir.path_;

  // Copy the desktop file into the root of the AppDir, if it isn't already there.
  const QString desktop_file = QFileInfo(desktop_file_path).fileName();
  const QString root_desktop_path = appdir.path_ + "/"_L1 + desktop_file;
  if (QFileInfo(desktop_file_path).absoluteFilePath() != QFileInfo(root_desktop_path).absoluteFilePath()) {
    if (!Utilities::CopyFile(desktop_file_path, root_desktop_path)) {
      error_message = u"Could not copy desktop file to AppDir root"_s;
      return nullopt;
    }
  }

  // Find the single top-level .desktop file.
  const QList<QFileInfo> fileinfos = QDir(appdir.path_).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  QStringList top_level_desktop_files;
  for (const QFileInfo &fileinfo : fileinfos) {
    if (fileinfo.fileName().endsWith(".desktop"_L1)) {
      top_level_desktop_files << fileinfo.absoluteFilePath();
    }
  }
  if (top_level_desktop_files.isEmpty()) {
    error_message = u"No desktop file was found, please place one into %1"_s.arg(appdir.path_);
    return nullopt;
  }
  if (top_level_desktop_files.size() > 1) {
    error_message = u"More than one desktop file was found in %1"_s.arg(appdir.path_);
    return std::nullopt;
  }
  appdir.desktop_file_path_ = top_level_desktop_files.first();

  DesktopEntry desktop_entry;
  if (!desktop_entry.Load(appdir.desktop_file_path_)) {
    error_message = u"Could not parse %1"_s.arg(appdir.desktop_file_path_);
    return nullopt;
  }

  const QString validation_error = desktop_entry.Validate();
  if (!validation_error.isEmpty()) {
    error_message = validation_error;
    return nullopt;
  }

  // Do not allow paths in the Exec= key.
  const QString exec_value = desktop_entry.Value(u"Exec"_s);
  const QString exec_first_token = exec_value.split(u' ').value(0);
  if (QFileInfo(exec_first_token).fileName() != exec_first_token) {
    error_message = u"Exec= contains a path, please remove it"_s;
    return nullopt;
  }
  appdir.main_executable_ = appdir.path_ + "/usr/bin/"_L1 + exec_first_token;

  // Do not allow paths in the Icon= key.
  const QString icon_value = desktop_entry.Value(u"Icon"_s);
  const QString icon_first_token = icon_value.split(u' ').value(0);
  if (QFileInfo(icon_first_token).fileName() != icon_first_token) {
    error_message = u"Icon= contains a path, please remove it"_s;
    return nullopt;
  }

  if (!appdir.CopyMainIconToRoot(icon_first_token, error_message)) return nullopt;

  return appdir;

}

bool AppDir::CopyMainIconToRoot(const QString &icon_name, QString &error_message) const {

  Q_UNUSED(error_message)

  if (QFileInfo::exists(path_ + "/"_L1 + icon_name + ".png"_L1)) {
    qInfo() << "Top-level icon already exists, leaving untouched";
    return true;
  }

  static const QList<int> kIconPreferenceOrder = {128, 256, 512, 48, 32, 24, 22, 16, 8};
  for (int size : kIconPreferenceOrder) {
    const QString candidate = u"%1/usr/share/icons/hicolor/%2x%2/apps/%3.png"_s.arg(path_).arg(size).arg(icon_name);
    if (QFileInfo::exists(candidate)) {
      Utilities::CopyFile(candidate, path_ + "/"_L1 + icon_name + ".png"_L1);
      break;
    }
  }

  return true;

}

QString AppDir::ElfInterpreter(QString &error_message) const {

  ElfFile elf_file(main_executable_);
  if (!elf_file.IsValid()) {
    error_message = u"Could not parse main executable %1 as an ELF file"_s.arg(main_executable_);
    return QString();
  }

  const QString interpreter = elf_file.Interpreter();
  if (interpreter.isEmpty()) {
    error_message = u"%1 has no ELF interpreter"_s.arg(main_executable_);
  }

  return interpreter;

}
