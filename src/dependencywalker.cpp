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

#include <utility>

#include <QString>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>

#include "elffile.h"
#include "excludelist.h"
#include "dependencywalker.h"

using std::as_const;
using namespace Qt::Literals::StringLiterals;

DependencyWalker::DependencyWalker(const AppImageDeployOptions options) : options_(options) {}

QStringList DependencyWalker::DirsFromSoConf(const QString &path) {

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringList();

  QStringList out;
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    const QString trimmed = stream.readLine().trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(u'#')) continue;
    if (trimmed.startsWith("include "_L1) || trimmed.startsWith("include\t"_L1)) {
      const QString rest = trimmed.mid(int("include"_L1.size())).trimmed();
      const QStringList tokens = rest.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
      for (const QString &token : tokens) {
        QString pattern = token;
        if (!pattern.startsWith(u'/')) {
          pattern = QFileInfo(path).absolutePath() + "/"_L1 + pattern;
        }
        const QString glob_dir = QFileInfo(pattern).absolutePath();
        const QString glob_pattern = QFileInfo(pattern).fileName();
        const QDir dir(glob_dir);
        const QStringList matches = dir.entryList({glob_pattern}, QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &match : matches) {
          out << DirsFromSoConf(glob_dir + "/"_L1 + match);
        }
      }
      continue;
    }
    if (trimmed.startsWith("hwcap "_L1) || trimmed.startsWith("hwcap\t"_L1)) {
      continue;  // Ignored by glibc too.
    }
    out << trimmed;
  }

  return out;

}

void DependencyWalker::EnsureDefaultLibraryLocations() {

  if (default_locations_added_) return;
  default_locations_added_ = true;

  // Mirrors the real dynamic linker's precedence (RPATH, already in library_locations_ from AppendLib by the time this first runs > LD_LIBRARY_PATH > RUNPATH > ld.so.cache/ld.so.conf > default trusted paths), plus $QTDIR/$QT_ROOT_DIR ranked above all of it: it's the most specific, deliberate override a caller can give us (e.g. pointing at a custom-built Qt), so it must win even over LD_LIBRARY_PATH, and it must be consulted here - not only in QtDeploy's plugin discovery - or a dependency on a same-named library (e.g. libQt6Core.so.6) resolves against whatever system copy happens to sit on one of the hardcoded paths below instead of the one the binary was actually built against.
  QString qtdir = qEnvironmentVariable("QTDIR");
  if (qtdir.isEmpty()) qtdir = qEnvironmentVariable("QT_ROOT_DIR");
  if (!qtdir.isEmpty()) {
    const QStringList suffixes = {"/lib64"_L1, "/lib"_L1};
    for (const QString &suffix : suffixes) {
      const QString clean_library_location = QDir::cleanPath(qtdir + suffix);
      if (!library_locations_.contains(clean_library_location)) library_locations_ << clean_library_location;
    }
  }

  const QString ld_library_path = qEnvironmentVariable("LD_LIBRARY_PATH");
  const QStringList ld_library_paths = ld_library_path.split(u':', Qt::SkipEmptyParts);
  for (const QString &library_location : ld_library_paths) {
    const QString clean_library_location = QDir::cleanPath(library_location);
    if (!library_locations_.contains(clean_library_location)) library_locations_ << clean_library_location;
  }

  if (QFileInfo::exists(u"/etc/ld.so.conf"_s)) {
    const QStringList dirs_from_so_conf = DirsFromSoConf(u"/etc/ld.so.conf"_s);
    for (const QString &library_location : dirs_from_so_conf) {
      const QString clean_library_location = QDir::cleanPath(library_location);
      if (!library_locations_.contains(clean_library_location)) library_locations_ << clean_library_location;
    }
  }

  // Genuine system directories come first: a guessed custom-Qt-prefix location has no business outranking the real system libraries just because it happens to also exist on disk - that guess should only ever be a last resort, not a default winner.
  // Anyone who actually wants a custom Qt build picked up says so explicitly, via $QTDIR/$QT_ROOT_DIR (above) or the binary's own rpath (already searched before this function ever runs).
  static const QStringList kDefaultLocations = {
      u"/lib64"_s,
      u"/usr/lib64"_s,
      u"/usr/local/lib64"_s,
      u"/usr/local/qt6"_s,
      u"/usr/local/qt6/lib64"_s,
  };
  for (const QString &library_location : kDefaultLocations) {
    const QString clean_library_location = QDir::cleanPath(library_location);
    if (!library_locations_.contains(clean_library_location)) library_locations_ << clean_library_location;
  }

}

QString DependencyWalker::FindLibrary(const QString &filename) {

  EnsureDefaultLibraryLocations();

  for (const QString &library_location : as_const(library_locations_)) {
    const QString candidate = library_location + "/"_L1 + filename;
    if (QFileInfo::exists(candidate)) return candidate;
  }

  return QString();

}

QStringList DependencyWalker::FindWithPrefixInLibraryLocations(const QString &prefix) {

  EnsureDefaultLibraryLocations();

  for (const QString &library_location : as_const(library_locations_)) {
    const QDir dir(library_location);
    if (!dir.exists()) continue;
    QStringList found;
    const QList<QFileInfo> fileinfos = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fileinfo : fileinfos) {
      if (fileinfo.fileName().startsWith(prefix)) found << fileinfo.absoluteFilePath();
    }
    if (!found.isEmpty()) return found;
  }

  return QStringList();

}

void DependencyWalker::AppendLib(const QString &path) {

  if (options_.testAnyFlag(AppImageDeployOption::UseExcludeList) && ExcludeList::Contains(QFileInfo(path).fileName())) return;

  // Any pre-existing rpath/runpath of this library is also a location we should search for its dependencies.
  ElfFile elf_file(path);
  if (elf_file.IsValid()) {
    const QStringList existing_rpaths = elf_file.ExistingRpaths();
    for (QString rpath : existing_rpaths) {
      rpath.replace("$ORIGIN"_L1, QFileInfo(path).absolutePath());
      const QString cleaned = QDir::cleanPath(rpath);
      if (!cleaned.isEmpty() && !library_locations_.contains(cleaned)) {
        qInfo() << "Add" << cleaned << "to the library_locations directories we search for libraries";
        library_locations_ << cleaned;
      }
    }
  }

  const QString clean_dir = QDir::cleanPath(QFileInfo(path).absolutePath());
  if (!library_locations_.contains(clean_dir)) library_locations_ << clean_dir;

  if (!all_elfs_.contains(path)) all_elfs_ << path;

}

QStringList DependencyWalker::FindAllExecutablesAndLibraries(const QString &path) {

  QStringList result;
  if (!QFileInfo(path).isDir()) {
    // A single file was requested for deployment directly; return it as-is.
    result << path;
    return result;
  }

  QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString candidate = it.next();
    if (ElfFile::IsElf(candidate)) result << candidate;
  }

  return result;

}

void DependencyWalker::GetDependencies(const QString &binary_or_lib) {

  if (seen_dependencies_.contains(binary_or_lib)) {
    qInfo() << "skipping already seen dep, circular ref" << binary_or_lib;
    return;
  }

  if (QFileInfo(binary_or_lib).isDir() || !QFileInfo::exists(binary_or_lib)) return;

  ElfFile elf_file(binary_or_lib);
  seen_dependencies_ << binary_or_lib;

  if (!elf_file.IsValid()) return;

  const QStringList imported_libraries = elf_file.ImportedLibraries();
  for (const QString &lib : imported_libraries) {
    const QString resolved = FindLibrary(lib);
    if (resolved.isEmpty()) {
      qWarning() << "Could not find library" << lib << "needed by" << binary_or_lib;
      continue;
    }
    if (all_elfs_.contains(resolved)) continue;
    AppendLib(resolved);
    GetDependencies(resolved);
  }

}

void DependencyWalker::AddPriorityElf(const QString &path) {

  AppendLib(path);
  GetDependencies(path);

}

void DependencyWalker::AddElfTree(const QString &path_to_dir_tree) {

  const QStringList binaries = FindAllExecutablesAndLibraries(path_to_dir_tree);
  for (const QString &lib : binaries) {
    bool already_resolved = false;
    const QString filename = QFileInfo(lib).fileName();
    for (const QString &existing : as_const(all_elfs_)) {
      if (QFileInfo(existing).fileName() == filename) {
        already_resolved = true;
        break;
      }
    }
    if (already_resolved) continue;
    AppendLib(lib);
  }
  for (const QString &elf_path : binaries) {
    GetDependencies(elf_path);
  }

  qInfo() << "library_locations:" << library_locations_;
  qInfo() << "len(all_elfs):" << all_elfs_.size();

}
