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

#include <QSet>
#include <QString>
#include <QIODevice>
#include <QFile>
#include <QTextStream>

#include "excludelist.h"

using namespace Qt::StringLiterals;

namespace {

const QSet<QString> &Entries() {

  static const QSet<QString> loaded = [] {
    QSet<QString> set;
    QFile file(u":/excludelist.txt"_s);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream stream(&file);
      while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        set.insert(line);
      }
    }
    return set;
  }();

  return loaded;

}

}  // namespace

namespace ExcludeList {

bool Contains(const QString &library_file_name) {
  return Entries().contains(library_file_name);
}

}  // namespace ExcludeList
