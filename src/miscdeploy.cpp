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
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>

#include "appdir.h"
#include "dependencywalker.h"
#include "utilities.h"
#include "miscdeploy.h"

using namespace Qt::Literals::StringLiterals;

namespace MiscDeploy {

bool HandleGlibSchemas(const AppDir &appdir, QString &error_message) {

  const QString schemas_dir = appdir.path() + "/usr/share/glib-2.0/schemas"_L1;
  if (!QDir(schemas_dir).exists()) return true;
  if (QFileInfo::exists(schemas_dir + "/gschemas.compiled"_L1)) return true;

  qInfo() << "Compiling glib-2.0 schemas...";

  QProcess process;
  process.setWorkingDirectory(schemas_dir);
  process.start(u"glib-compile-schemas"_s, {u"."_s});
  if (!process.waitForFinished(-1) || process.exitCode() != 0) {
    error_message = u"glib-compile-schemas failed: %1"_s.arg(QString::fromUtf8(process.readAllStandardError()));
    return false;
  }

  return true;

}

bool DeployFontconfig(const AppDir &appdir, QString &error_message) {

  const QString fonts_dir = appdir.path() + "/etc/fonts"_L1;
  if (QFileInfo::exists(fonts_dir)) return true;

  qInfo() << "Adding fontconfig symlink...";

  if (!QDir().mkpath(fonts_dir)) {
    error_message = u"Could not create %1"_s.arg(fonts_dir);
    return false;
  }
  if (!QFile::link(u"/etc/fonts/fonts.conf"_s, fonts_dir + "/fonts.conf"_L1)) {
    error_message = u"Could not symlink %1/fonts.conf"_s.arg(fonts_dir);
    return false;
  }

   return true;

}

bool HandleGdk(DependencyWalker &dependency_walker, const AppDir &appdir, QString &error_message) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libgdk_pixbuf"_L1)) continue;

    qInfo() << "Determining Gdk pixbuf loaders (for GDK_PIXBUF_MODULEDIR and GDK_PIXBUF_MODULE_FILE)...";

    const QStringList locations = dependency_walker.FindWithPrefixInLibraryLocations(u"gdk-pixbuf"_s);
    if (locations.isEmpty()) {
      error_message = u"Could not find Gdk pixbuf loaders"_s;
      return false;
    }

    for (const QString &location : locations) {
      dependency_walker.AddElfTree(location);

      const QStringList loaders_caches = Utilities::FilesWithSuffixRecursive(location, u"loaders.cache"_s);
      if (loaders_caches.isEmpty()) {
        error_message = u"Could not find loaders.cache under %1"_s.arg(location);
        return false;
      }
      const QString loaders_cache = loaders_caches.first();
      const QString loaders_cache_in_appdir = Utilities::AppDirDestinationForCache(loaders_cache, appdir);
      if (loaders_cache_in_appdir.isEmpty()) continue;  // Already deployed by a previous run.

      if (!Utilities::CopyFile(loaders_cache, loaders_cache_in_appdir)) {
        error_message = u"Could not copy %1"_s.arg(loaders_cache);
        return false;
      }

      const QString resolved_dir = Utilities::EvalSymlinkDir(QFileInfo(loaders_cache).absolutePath());
      const QString what_to_patch_away = resolved_dir + "/loaders/"_L1;
      qInfo() << "Patching" << loaders_cache_in_appdir << "removing" << what_to_patch_away;
      Utilities::PatchFile(loaders_cache_in_appdir, what_to_patch_away.toUtf8(), QByteArray());
    }

    break;
  }

  return true;

}

bool DeployGtkDirectory(DependencyWalker &dependency_walker, const AppDir &appdir, int gtk_version, QString &error_message) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith(u"libgtk-%1"_s.arg(gtk_version))) continue;

    qInfo() << "Bundling Gtk" << gtk_version << "directory (for GTK_EXE_PREFIX)...";

    const QStringList locations = dependency_walker.FindWithPrefixInLibraryLocations(u"gtk-%1"_s.arg(gtk_version));
    if (locations.isEmpty()) {
      error_message = u"Could not find Gtk %1 directory"_s.arg(gtk_version);
      return false;
    }

    for (const QString &location : locations) {
      qInfo() << "Bundling dependencies of Gtk" << gtk_version << "directory...";
      dependency_walker.AddElfTree(location);

      if (gtk_version <= 3) {
        qInfo() << "Bundling Default theme for Gtk" << gtk_version << "(for GTK_THEME=Default)...";
        const QString theme_dir = u"/usr/share/themes/Default/gtk-%1.0"_s.arg(gtk_version);
        if (QFileInfo::exists(theme_dir)) {
          Utilities::CopyTree(theme_dir, appdir.path() + theme_dir);
        }

        qInfo() << "Bundling immodules.cache for Gtk" << gtk_version;
        const QStringList immodules_caches = Utilities::FilesWithSuffixRecursive(location, u"immodules.cache"_s);
        if (!immodules_caches.isEmpty()) {
          const QString immodules_cache = immodules_caches.first();
          const QString immodules_cache_in_appdir = Utilities::AppDirDestinationForCache(immodules_cache, appdir);
          if (!immodules_cache_in_appdir.isEmpty()) {
            Utilities::CopyFile(immodules_cache, immodules_cache_in_appdir);

            const QString resolved_dir = Utilities::EvalSymlinkDir(QFileInfo(immodules_cache).absolutePath());
            const QString what_to_patch_away = resolved_dir + "/immodules/"_L1;
            qInfo() << "Patching" << immodules_cache_in_appdir << "removing" << what_to_patch_away;
            Utilities::PatchFile(immodules_cache_in_appdir, what_to_patch_away.toUtf8(), QByteArray());
          }
        }
      }
    }

    break;
  }

  return true;

}

void HandleAlsa(DependencyWalker &dependency_walker) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libasound.so"_L1)) continue;

    qInfo() << "Bundling alsa-lib directory...";

    const QStringList locations = dependency_walker.FindWithPrefixInLibraryLocations(u"alsa-lib"_s);
    if (locations.isEmpty()) {
      qWarning() << "Could not find alsa-lib directory";
      qWarning() << "E.g., in Alpine Linux: apk add alsa-plugins alsa-plugins-pulse";
    }
    else {
      qInfo() << "Bundling dependencies of alsa-lib directory...";
      dependency_walker.AddElfTree(locations.first());
    }

    break;
  }

}

void HandleGioModules(DependencyWalker &dependency_walker, const AppDir &appdir) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libgio-2.0.so"_L1)) continue;

    qInfo() << "Bundling GIO modules directory (for GIO_EXTRA_MODULES)...";

    // Resolved the same way GIO itself would look for extra modules: $GIO_EXTRA_MODULES first, then pkg-config's gio-2.0 libdir (queried on the very machine whose GIO is being bundled, so it reflects that distro's actual layout), then falling back to scanning the locations DependencyWalker already found libgio-2.0 in.
    QString modules_dir;

    const QString from_env = qEnvironmentVariable("GIO_EXTRA_MODULES");
    if (!from_env.isEmpty()) {
      const QStringList paths = from_env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
      for (const QString &path : paths) {
        if (QDir(path).exists()) {
          modules_dir = path;
          break;
        }
      }
    }

    if (modules_dir.isEmpty()) {
      const QString libdir = Utilities::PkgConfigVariable(u"gio-2.0"_s, u"libdir"_s);
      if (!libdir.isEmpty()) {
        const QString candidate = libdir + "/gio/modules"_L1;
        if (QDir(candidate).exists()) modules_dir = candidate;
      }
    }

    // Not done via FindWithPrefixInLibraryLocations("gio"): that does a fuzzy filename-prefix scan of whichever library_location is searched first, and "gio" is short and generic enough to falsely match an unrelated file (e.g. the app's own binary, if its name happens to start with "gio") before ever reaching the real gio/ directory. Checking for the exact "gio/modules" subpath avoids that.
    if (modules_dir.isEmpty()) {
      for (const QString &location : dependency_walker.library_locations()) {
        const QString candidate = location + "/gio/modules"_L1;
        if (QDir(candidate).exists()) {
          modules_dir = candidate;
          break;
        }
      }
    }

    if (modules_dir.isEmpty()) {
      qWarning() << "Could not find GIO modules directory";
      break;
    }

    qInfo() << "Bundling dependencies of GIO modules directory...";
    dependency_walker.AddElfTree(modules_dir);

    // Unlike loaders.cache/immodules.cache, giomodule.cache holds only bare module filenames, not absolute paths, so it needs copying but no patching.
    const QStringList caches = Utilities::FilesWithSuffixRecursive(modules_dir, u"giomodule.cache"_s);
    for (const QString &cache : caches) {
      const QString cache_in_appdir = Utilities::AppDirDestinationForCache(cache, appdir);
      if (!cache_in_appdir.isEmpty()) Utilities::CopyFile(cache, cache_in_appdir);
    }

    break;
  }

}

void HandleGnuTls(DependencyWalker &dependency_walker, const AppDir &appdir) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libgnutls.so"_L1)) continue;

    qInfo() << "Bundling GnuTLS system priority/crypto-policy config (for GNUTLS_SYSTEM_PRIORITY_FILE)...";

    // This is the path openSUSE's (and Fedora's/RHEL's) crypto-policies package installs, and the compiled-in default our GnuTLS build falls back to.
    // Debian/Ubuntu don't ship this mechanism at all, so on those hosts the bundled GnuTLS otherwise fails outright with "Failed to set GnuTLS session priority with error beginning at %COMPAT" - bundling our own copy at this same path within the AppDir and having AppRun point GNUTLS_SYSTEM_PRIORITY_FILE at it sidesteps needing this exact host path to exist at all.
    static const QString kHostPriorityFile = u"/etc/crypto-policies/back-ends/gnutls.config"_s;
    if (QFileInfo::exists(kHostPriorityFile)) {
      const QString resolved = QFileInfo(kHostPriorityFile).canonicalFilePath();
      const QString source = resolved.isEmpty() ? kHostPriorityFile : resolved;
      if (!Utilities::CopyFile(source, appdir.path() + kHostPriorityFile)) {
        qWarning() << "Could not copy" << source;
      }
    }
    else {
      qWarning() << "Could not find" << kHostPriorityFile << "on this build host; the bundled GnuTLS may fail with '%COMPAT' priority errors on a host that doesn't share this exact crypto-policy path";
    }

    break;
  }

}

void HandlePulseAudio(DependencyWalker &dependency_walker) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libpulse.so"_L1)) continue;
    qInfo() << "Bundling pulseaudio directory...";
    const QStringList locations = dependency_walker.FindWithPrefixInLibraryLocations(u"pulseaudio"_s);
    if (locations.isEmpty()) {
      qWarning() << "Could not find pulseaudio directory";
      return;
    }
    qInfo() << "Bundling dependencies of pulseaudio directory...";
    dependency_walker.AddElfTree(locations.first());
    break;
  }

}

bool HandleNvidia(const DependencyWalker &dependency_walker, QString &error_message) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (QFileInfo(lib).fileName().startsWith("libnvidia"_L1)) {
      error_message = u"System (most likely libGL) uses libnvidia*; please build on another system that does not use NVIDIA drivers"_s;
      return false;
    }
  }

  return true;

}

}  // namespace MiscDeploy
