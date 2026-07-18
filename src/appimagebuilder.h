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

#ifndef APPIMAGEBUILDER_H
#define APPIMAGEBUILDER_H

#include <QString>

// Turns an already-deployed AppDir into a .AppImage file: validates the AppDir (AppRun, single top-level .desktop file, optional AppStream metainfo),
// resolves/converts its icon, stamps the desktop file with X-AppImage-Version, then runs mksquashfs and embeds the AppImage runtime at offset 0.
namespace AppImageBuilder {

struct Options {
  // Output path. If empty, defaults to "<Name>-<version>-Linux-<arch>.AppImage" in the current directory (or inside `destination` if it names an existing directory).
  QString destination;

  // Path to the AppImage runtime binary to embed. If empty, looked up as "runtime-<arch>" next to the executable, or under ../share/AppImageKit/runtime/ relative to it.
  QString runtime_file;

  // mksquashfs -comp value.
  QString squashfs_compression_type = QStringLiteral("zstd");

  // If true (the default) and the AppDir ships an AppStream metainfo file, validate it with `appstreamcli validate-tree`.
  bool check_appstream_metadata = true;

  // Version string to stamp into X-AppImage-Version and use in the default output filename. If empty, detected by running the AppDir's main executable (from Exec= in its .desktop file) with --version and extracting a version-like token from its output.
  QString version;
};

// Returns false and sets error_message on failure. On success, output_path (if non-null) receives the absolute path of the produced .AppImage.
bool Build(const QString &app_dir_path, const Options &options, QString &output_path, QString &error_message);

}  // namespace AppImageBuilder

#endif  // APPIMAGEBUILDER_H
