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

#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>

#include <QByteArray>
#include <QString>
#include <QStringList>

class AppDir;

namespace Utilities {

QStringList SplitPaths(const std::string &joined);

// Non-recursive: only directly within `directory`, matching a filename prefix.
QStringList FilesWithPrefix(const QString &directory, const QString &prefix);

// Non-recursive: only directly within `directory`.
QStringList FilesWithSuffix(const QString &directory, const QString &suffix);

// Recursively lists all regular files under `directory` whose name ends with `suffix`.
QStringList FilesWithSuffixRecursive(const QString &directory, const QString &suffix);

// Replaces every occurrence of `search` with `replace` in the file at `path` (byte-for-byte, not text-aware) and writes the result back in place.
bool PatchFile(const QString &path, const QByteArray &search, const QByteArray &replace);

// Returns the byte offset of the first occurrence of `needle` in the file at `path`, searching from `start_offset`, or -1 if not found.
qint64 ScanFileForBytes(const QString &path, const QByteArray &needle, const qint64 start_offset = 0);

QString ReadNullTerminatedStringAfter(const QString &path, const QByteArray &marker);

// Copies src to dst, creating dst's parent directory as needed and resolving src if it is a symlink.
bool CopyFile(const QString &src, const QString &dst);

// Copies a directory tree recursively (like `cp -r`), creating dst as needed.
bool CopyTree(const QString &src, const QString &dst);

// Writes the contents of input_path into the (already existing) file at output_path at `offset`, without truncating it.
bool WriteFileIntoOtherFileAtOffset(const QString &input_path, const QString &output_path, const qint64 offset);

// Rewrites a host-absolute path to where it should land inside the AppDir, so every shared library ends up merged under a single usr/lib64 regardless of which real directory the host resolved it to:
//  - "/lib64/..." collapses onto "usr/lib64/..." (the host's top-level /lib64, e.g. a merged-/usr symlink target, is not mirrored as its own top-level dir in the AppDir).
//  - If `qt_prefix_path` is non-empty (the Qt install directory found by QtDeploy, e.g. "/usr/local/qt6"): "<qt_prefix_path>/lib64/..." also collapses onto "usr/lib64/...", merging Qt's own libraries in with everything else; anything else under `qt_prefix_path` (plugins/, qml/, ...) instead lands under "usr/lib64/<qt_prefix_path's own directory name>/...", e.g. "usr/lib64/qt6/plugins/...".
// Paths outside those cases are returned unchanged.
QString RemapAppDirPath(const QString &path, const QString &qt_prefix_path);

// filepath.Dir() applied to a file path strips the last path component. QDir::cdUp() on the file's containing directory does the same, one level per call.
QString DirUp(const QString &path, const int levels);

QString DetectArchitecture(const QString &app_dir_path, QString &error_message);

// Runs `executable_path` with a single "--version" argument and extracts a version-like token (e.g. "1.2.24" or "1.2.24-11-g754b469c4") from the last non-empty line of its combined stdout/stderr output. Returns an empty string and sets *error_message on failure (execution failure, timeout, or no parseable version token found).
QString DetectVersionFromExecutable(const QString &executable_path, QString &error_message);

QString EvalSymlinkDir(const QString &path);

// Where `source_path` should be copied to inside the AppDir, or an empty string if it's already there. On a re-run against an already-deployed AppDir, functions like FindWithPrefixInLibraryLocations()/FilesWithSuffixRecursive() can resolve to a file that's already inside appdir.path() (left over from a previous run) rather than a genuine host path; blindly prepending appdir.path() again would nest a whole duplicate copy of the AppDir inside itself.
QString AppDirDestinationForCache(const QString &source_path, const AppDir &appdir);

bool CheckMksquashfsVersion(QString &error_message);

bool ConvertSvgToPng(const QString &svg_path, const QString &png_path, const int size);

}  // namespace Utilities

#endif  // UTILITIES_H
