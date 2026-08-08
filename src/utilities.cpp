/*
 * AppImage Tools
 * Copyright 2024-2026, Jonas Kvinge <jonas@jkvinge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in_file the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QList>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QDebug>

#include "utilities.h"
#include "elffile.h"
#include "appdir.h"

using std::string;
using namespace Qt::StringLiterals;

namespace Utilities {

QStringList SplitPaths(const string &joined_paths) {

  QStringList paths;
  const QString qjoined_paths = QString::fromStdString(joined_paths);
  const QStringList parts = qjoined_paths.split(u':', Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    paths << part;
  }

  return paths;

}

QStringList FilesWithPrefix(const QString &directory, const QString &prefix) {

  QStringList files;
  const QDir dir(directory);
  const QList<QFileInfo> fileinfos = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &fileinfo : fileinfos) {
    if (fileinfo.fileName().startsWith(prefix)) {
      files << fileinfo.absoluteFilePath();
    }
  }

  return files;

}

QStringList FilesWithSuffix(const QString &directory, const QString &suffix) {

  QStringList files;
  const QDir dir(directory);
  const QList<QFileInfo> fileinfos = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo &fileinfo : fileinfos) {
    if (fileinfo.fileName().endsWith(suffix)) {
      files << fileinfo.absoluteFilePath();
    }
  }

  return files;

}

QStringList FilesWithSuffixRecursive(const QString &directory, const QString &suffix) {

  QStringList files;
  QDirIterator it(directory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    if (path.endsWith(suffix)) {
      files << path;
    }
  }

  return files;

}

bool PatchFile(const QString &path, const QByteArray &search, const QByteArray &replace) {

  QFile in_file(path);
  if (!in_file.open(QIODevice::ReadOnly)) return false;
  QByteArray content = in_file.readAll();
  in_file.close();

  content.replace(search, replace);

  const QString tmp_path = path + ".patched"_L1;
  QFile out_file(tmp_path);
  if (!out_file.open(QIODevice::WriteOnly)) return false;
  out_file.write(content);
  out_file.close();

  // Preserve permissions, matching os.WriteFile(path+".patched", output, fi.Mode().Perm())
  QFile::setPermissions(tmp_path, QFileInfo(path).permissions());

  QFile::remove(path);

  return QFile::rename(tmp_path, path);

}

qint64 ScanFileForBytes(const QString &path, const QByteArray &needle, const qint64 start_offset) {

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return -1;
  const QByteArray content = file.readAll();
  return content.indexOf(needle, start_offset);

}

QString ReadNullTerminatedStringAfter(const QString &path, const QByteArray &marker) {

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QString();

  const QByteArray content = file.readAll();
  const int marker_pos = content.indexOf(marker);
  if (marker_pos < 0) return QString();

  const int start = marker_pos + marker.size();
  int end = content.indexOf('\0', start);
  if (end < 0) end = content.size();

  return QString::fromUtf8(content.mid(start, end - start)).trimmed();

}

bool CopyFile(const QString &src, const QString &dst) {

  const QFileInfo src_info(src);
  const QString resolved_src = src_info.isSymLink() ? src_info.symLinkTarget() : src;

  QDir().mkpath(QFileInfo(dst).absolutePath());

  if (QFile::exists(dst)) {
    QFile::remove(dst);
  }

  return QFile::copy(resolved_src, dst);

}

bool CopyTree(const QString &src, const QString &dst) {

  const QFileInfo src_info(src);
  if (!src_info.exists()) return false;
  if (src_info.isSymLink() && !src_info.isDir()) return CopyFile(src, dst);
  if (!src_info.isDir()) return CopyFile(src, dst);

  QDir().mkpath(dst);
  const QDir dir(src);
  bool ok = true;
  const QList<QFileInfo> fileinfos = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &fileinfo : fileinfos) {
    const QString child_dst = dst + "/"_L1 + fileinfo.fileName();
    if (fileinfo.isDir() && !fileinfo.isSymLink()) {
      ok = CopyTree(fileinfo.absoluteFilePath(), child_dst) && ok;
    }
    else {
      ok = CopyFile(fileinfo.absoluteFilePath(), child_dst) && ok;
    }
  }

  return ok;

}

QString RemapAppDirPath(const QString &path, const QString &qt_prefix_path) {

  if (!qt_prefix_path.isEmpty()) {
    const QString qt_lib64_dir = qt_prefix_path + "/lib64"_L1;
    if (path == qt_lib64_dir) return "/usr/lib64"_L1;
    if (path.startsWith(qt_lib64_dir + "/"_L1)) return "/usr/lib64"_L1 + path.mid(qt_lib64_dir.size());

    const QString qt_dir_name = QFileInfo(qt_prefix_path).fileName();
    if (path == qt_prefix_path) return "/usr/lib64/"_L1 + qt_dir_name;
    if (path.startsWith(qt_prefix_path + "/"_L1)) return "/usr/lib64/"_L1 + qt_dir_name + path.mid(qt_prefix_path.size());
  }

  if (path == "/lib64"_L1) return "/usr/lib64"_L1;
  if (path.startsWith("/lib64/"_L1)) return "/usr/lib64"_L1 + path.mid("/lib64"_L1.size());

  return path;

}

bool WriteFileIntoOtherFileAtOffset(const QString &input_path, const QString &output_path, const qint64 offset) {

  QFile in_file(input_path);
  if (!in_file.open(QIODevice::ReadOnly)) return false;

  QFile out_file(output_path);
  if (!out_file.open(QIODevice::ReadWrite)) return false;
  if (!out_file.seek(offset)) return false;

  constexpr qint64 kChunkSize = 1 << 20;  // 1 MiB
  while (!in_file.atEnd()) {
    const QByteArray chunk = in_file.read(kChunkSize);
    if (out_file.write(chunk) != chunk.size()) return false;
  }

  return true;

}

QString DirUp(const QString &path, const int levels) {

  QDir dir(QFileInfo(path).absolutePath());
  for (int i = 1; i < levels; ++i) {
    dir.cdUp();
  }

  return dir.absolutePath();

}

QString DetectArchitecture(const QString &app_dir_path, QString &error_message) {

  const QString arch_env = qEnvironmentVariable("ARCH");
  if (!arch_env.isEmpty()) {
    qInfo() << "Architecture from $ARCH:" << arch_env;
    return arch_env;
  }

  QStringList archs;
  ElfFile app_run_elf(app_dir_path + "/AppRun"_L1);
  if (app_run_elf.IsValid() && !app_run_elf.Architecture().isEmpty()) {
    qInfo() << "Architecture from AppRun:" << app_run_elf.Architecture();
    archs << app_run_elf.Architecture();
  }
  else {
    QDirIterator it(app_dir_path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      if (!path.contains(".so."_L1)) continue;
      ElfFile elf(path);
      if (!elf.IsValid()) continue;
      const QString arch = elf.Architecture();
      if (arch.isEmpty()) continue;
      if (!archs.contains(arch)) {
        qInfo() << "Architecture of" << QFileInfo(path).fileName() << ":" << arch;
        archs << arch;
      }
    }
  }

  if (archs.size() != 1) {
    error_message = u"Could not determine architecture automatically, please supply it as $ARCH"_s;
    return QString();
  }

  return archs.first();

}

QString DetectVersionFromExecutable(const QString &executable_path, QString &error_message) {

  qInfo() << "Running" << executable_path << "--version to detect the application version...";

  QProcess process;
  process.start(executable_path, {u"--version"_s});
  if (!process.waitForFinished(10000)) {
    process.kill();
    process.waitForFinished(-1);
    error_message = u"Timed out running %1 --version"_s.arg(executable_path);
    return QString();
  }

  const QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed();
  const QStringList lines = output.split(u'\n', Qt::SkipEmptyParts);
  if (lines.isEmpty()) {
    error_message = u"'%1 --version' produced no output"_s.arg(executable_path);
    return QString();
  }

  static const QRegularExpression kVersionRegex(u"\\d+(?:\\.\\d+){1,3}(?:-[A-Za-z0-9.]+)*"_s);
  const QRegularExpressionMatch match = kVersionRegex.match(lines.last());
  if (!match.hasMatch()) {
    error_message = u"Could not parse a version number out of '%1 --version' output: '%2'"_s.arg(executable_path, lines.last());
    return QString();
  }

  const QString version = match.captured(0);
  qInfo() << "Detected version" << version << "from" << executable_path;

  return version;

}

QString EvalSymlinkDir(const QString &path) {

  const QString canonical_path = QFileInfo(path).canonicalFilePath();
  return canonical_path.isEmpty() ? path : canonical_path;

}

QString AppDirDestinationForCache(const QString &source_path, const AppDir &appdir) {

  if (source_path.startsWith(appdir.path())) return QString();
  return appdir.path() + Utilities::RemapAppDirPath(source_path, QString());

}

bool CheckMksquashfsVersion(QString &error_message) {

  if (QStandardPaths::findExecutable(u"mksquashfs"_s).isEmpty()) {
    error_message = u"Required helper tool 'mksquashfs' is missing"_s;
    return false;
  }

  QProcess proc;
  proc.start(u"mksquashfs"_s, {u"-version"_s});
  if (!proc.waitForFinished(-1)) {
    error_message = u"Could not run mksquashfs -version"_s;
    return false;
  }

  // Interestingly, some `mksquashfs -version` builds exit non-zero anyway; only the presence of "version" in the output is checked.
  const QString out = QString::fromUtf8(proc.readAllStandardOutput() + proc.readAllStandardError());
  if (!out.contains("version"_L1)) {
    error_message = u"Could not determine mksquashfs version"_s;
    return false;
  }

  const QStringList parts = out.split(u' ');
  const int idx = parts.indexOf(u"version"_s);
  if (idx < 0 || idx + 1 >= parts.size()) {
    error_message = u"Could not parse mksquashfs version"_s;
    return false;
  }

  QString ver_string = parts.at(idx + 1);
  ver_string = ver_string.section(u'-', 0, 0);
  const QVersionNumber ver = QVersionNumber::fromString(ver_string);
  if (ver < QVersionNumber(4, 4)) {
    error_message = u"mksquashfs on $PATH is version %1, but at least 4.4 is required (for -offset)"_s.arg(ver_string);
    return false;
  }

  return true;

}

bool ConvertSvgToPng(const QString &svg_path, const QString &png_path, const int size) {

  QSvgRenderer renderer(svg_path);
  if (!renderer.isValid()) return false;

  QSize target_size = renderer.defaultSize();
  if (target_size.isEmpty()) {
    target_size = QSize(size, size);
  }
  else {
    target_size.scale(size, size, Qt::KeepAspectRatio);
  }

  QImage image(target_size, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  renderer.render(&painter);
  painter.end();

  return image.save(png_path, "PNG");

}

QString PkgConfigVariable(const QString &package, const QString &variable) {

  QProcess process;
  process.start(u"pkg-config"_s, {u"--variable=%1"_s.arg(variable), package});
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(-1);
    return QString();
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return QString();

  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();

}

}  // namespace Utilities
