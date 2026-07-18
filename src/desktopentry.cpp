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

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "desktopentry.h"

using namespace Qt::Literals::StringLiterals;

bool DesktopEntry::Load(const QString &path) {

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  path_ = path;
  lines_.clear();

  QTextStream stream(&file);
  while (!stream.atEnd()) {
    lines_ << stream.readLine();
  }
  file.close();

  Reindex();

  return section_start_ >= 0;

}

void DesktopEntry::Reindex() {

  section_start_ = -1;
  section_end_ = -1;
  values_.clear();
  key_line_index_.clear();

  for (int i = 0; i < lines_.size(); ++i) {
    const QString trimmed = lines_[i].trimmed();
    if (trimmed == "[Desktop Entry]"_L1) {
      section_start_ = i;
      continue;
    }
    if (section_start_ < 0) continue;  // Haven't found the section yet.
    if (trimmed.startsWith(u'[') && i != section_start_) {
      section_end_ = i;
      break;
    }
    if (trimmed.isEmpty() || trimmed.startsWith(u'#')) continue;
    const int eq = trimmed.indexOf(u'=');
    if (eq <= 0) continue;
    const QString key = trimmed.left(eq).trimmed();
    const QString value = trimmed.mid(eq + 1);
    values_.insert(key, value);
    key_line_index_.insert(key, i);
  }

  if (section_start_ >= 0 && section_end_ < 0) {
    section_end_ = lines_.size();
  }

}

bool DesktopEntry::HasKey(const QString &key) const {
  return values_.contains(key);
}

QString DesktopEntry::Value(const QString &key) const {
  return values_.value(key);
}

void DesktopEntry::SetKey(const QString &key, const QString &value) {

  values_.insert(key, value);

  const QString line = key + "="_L1 + value;
  if (key_line_index_.contains(key)) {
    lines_[key_line_index_.value(key)] = line;
  }
  else if (section_start_ >= 0) {
    // Insert right after the [Desktop Entry] header.
    const int insert_at = section_start_ + 1;
    lines_.insert(insert_at, line);
    section_end_ += 1;
    Reindex();
  }

}

bool DesktopEntry::Save(const QString &path) const {

  const QString target = path.isEmpty() ? path_ : path;
  QFile file(target);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

  QTextStream stream(&file);
  for (const QString &line : lines_) {
    stream << line << '\n';
  }

  return true;

}

QString DesktopEntry::Validate() const {

  static const QStringList kNeededKeys = { u"Categories"_s, u"Name"_s, u"Exec"_s, u"Type"_s, u"Icon"_s };
  for (const QString &key : kNeededKeys) {
    if (!HasKey(key)) return u".desktop file is missing a '%1'= key"_s.arg(key);
  }

  const QString icon_name = Value(u"Icon"_s);
  if (icon_name.contains(u'/')) return u"Desktop file contains Icon= entry with a path"_s;

  const QFileInfo icon_info(icon_name);
  const QString suffix = icon_info.suffix().toLower();
  if (suffix == "png"_L1 || suffix == "svg"_L1 || suffix == "svgz"_L1 || suffix == "xpm"_L1) {
    return u"Desktop file contains Icon= entry with a suffix, please remove the suffix"_s;
  }

  return QString();

}
