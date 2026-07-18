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

#ifndef DESKTOPENTRY_H
#define DESKTOPENTRY_H

#include <QHash>
#include <QString>
#include <QStringList>

// A minimal reader/writer for the "[Desktop Entry]" section of a freedesktop.org .desktop file. Deliberately hand-rolled rather than built on QSettings: desktop-entry values like `Categories=A;B;` use ';' as a plain value separator, which QSettings' INI parsing does not handle the way desktop files intend.
//
// Edits are applied as targeted single-line replacements/insertions so that untouched keys, comments, and key ordering in the file are preserved.
class DesktopEntry {
 public:
  DesktopEntry() = default;

  // Loads and parses `path`. Returns false (and leaves the object empty) if the file could not be read or has no "[Desktop Entry]" section.
  bool Load(const QString &path);

  bool HasKey(const QString &key) const;
  QString Value(const QString &key) const;

  // Sets `key` to `value` in the "Desktop Entry" section, updating the in-memory line buffer. Call Save() to persist.
  void SetKey(const QString &key, const QString &value);

  // Writes the (possibly modified) file back to `path` (defaults to the path passed to Load()).
  bool Save(const QString &path = QString()) const;

  // Checks for the presence of the keys every AppImage desktop file needs (Categories, Name, Exec, Type, Icon) and that Icon= has neither a path nor a filename suffix.
  // Returns an empty string if valid, otherwise a human-readable error.
  QString Validate() const;

 private:
  void Reindex();

 private:
  QString path_;
  QStringList lines_;
  int section_start_ = -1;  // index of the "[Desktop Entry]" line
  int section_end_ = -1;    // index one past the last line belonging to the section
  QHash<QString, QString> values_;
  QHash<QString, int> key_line_index_;  // key -> index into lines_, if already present
};

#endif  // DESKTOPENTRY_H
