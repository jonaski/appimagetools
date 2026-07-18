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

#ifndef DEPENDENCYWALKER_H
#define DEPENDENCYWALKER_H

#include <QString>
#include <QStringList>

#include "appimagedeployoptions.h"

// Walks the ELF dependency graph rooted at one or more paths (executables, libraries, or whole directory trees), building up:
//  - the full set of ELF files that need to end up in the AppDir, and
//  - the set of directories they were found in / reference via rpath, which becomes the search path for resolving further DT_NEEDED entries.
class DependencyWalker {
 public:
  explicit DependencyWalker(const AppImageDeployOptions options);

  const QStringList &all_elfs() const { return all_elfs_; }
  const QStringList &library_locations() const { return library_locations_; }

  // Registers `path` and resolves its dependencies (recursively) immediately, using its own rpath/runpath ahead of anything else - meant to be called for the app's actual main executable before AddElfTree() walks the rest of the (possibly already partially-populated, from a prior deploy run) prefix tree, so a dependency that's genuinely reachable via the executable's own linkage always wins over a same-named file that merely happens to already exist somewhere else under the prefix.
  void AddPriorityElf(const QString &path);

  // Finds every ELF under `path_to_dir_tree` (or, if it names a single file, just that file), registers each one (and, recursively, everything each one depends on) into all_elfs()/library_locations().
  // Skips any file whose name matches something already in all_elfs() (typically from a prior AddPriorityElf() call) - a stale or otherwise incidental same-named file elsewhere in the tree must not override a dependency that's already been correctly resolved.
  void AddElfTree(const QString &path_to_dir_tree);

  // Searches library_locations() for files/directories whose name starts with `prefix`. Returns an empty list if nothing matched.
  QStringList FindWithPrefixInLibraryLocations(const QString &prefix);

  // Searches every directory already in library_locations() (rpaths of ELFs already processed), then $QTDIR/$QT_ROOT_DIR, then $LD_LIBRARY_PATH, then ld.so.conf directories, then the standard system library directories, for a library file named exactly `filename`.
  // With $QTDIR/$QT_ROOT_DIR added ahead of everything but rpath so a custom Qt build's own libraries are preferred over a same-named system copy. Returns an empty string if not found.
  QString FindLibrary(const QString &filename);

 private:
  void EnsureDefaultLibraryLocations();
  void AppendLib(const QString &path);
  void GetDependencies(const QString &binary_or_lib);
  static QStringList FindAllExecutablesAndLibraries(const QString &path);
  static QStringList DirsFromSoConf(const QString &path);

  const AppImageDeployOptions options_;
  QStringList all_elfs_;
  QStringList library_locations_;
  QStringList seen_dependencies_;
  bool default_locations_added_ = false;
};

#endif  // DEPENDENCYWALKER_H
