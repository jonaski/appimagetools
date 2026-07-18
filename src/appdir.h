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

#ifndef APPDIR_H
#define APPDIR_H

#include <optional>

#include <QString>

// Represents an AppDir being deployed, anchored at the top-level directory that contains usr/bin, usr/share/applications/*.desktop, etc.
class AppDir {
 public:
  // Locates the AppDir root from `desktop_file_path` (expected to be at <AppDir>/usr/share/applications/foo.desktop), copies that desktop file to the AppDir root if not already there, validates it (required keys, no paths in Exec=/Icon=), and ensures a top-level icon is present.
  // Returns std::nullopt and sets error_message on any failure.
  static std::optional<AppDir> Create(const QString &desktop_file_path, QString &error_message);

  QString path() const { return path_; }
  QString desktop_file_path() const { return desktop_file_path_; }
  QString main_executable() const { return main_executable_; }

  // Copies the best-matching hicolor icon for `icon_name` to <AppDir>/icon_name.png if one isn't already there.
  // Preference order (largest usable first): 128, 256, 512, 48, 32, 24, 22, 16, 8.
  bool CopyMainIconToRoot(const QString &icon_name, QString &error_message) const;

  // PT_INTERP of main_executable(), via ElfFile.
  QString ElfInterpreter(QString &error_message) const;

 private:
  QString path_;
  QString desktop_file_path_;
  QString main_executable_;
};

#endif  // APPDIR_H
