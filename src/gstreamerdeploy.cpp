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
#include <QHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

#include "gstreamerdeploy.h"
#include "dependencywalker.h"
#include "elffile.h"
#include "utilities.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Every ELF file under `plugin_dir`, found the same way DependencyWalker::AddElfTree() would (recursively, ELF-ness checked via ElfFile::IsElf) - kept independent of that class so plugin selection can happen before any of these are registered with the walker.
QStringList FindPluginFiles(const QString &plugin_dir) {

  QStringList result;
  QDirIterator it(plugin_dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString candidate = it.next();
    if (ElfFile::IsElf(candidate)) {
      result << candidate;
    }
  }

  return result;

}

// Per-plugin-file classification, aggregated across every element the plugin provides (a single .so commonly provides several - e.g. libgstplayback.so provides decodebin/playbin, both Klass "Generic/Bin/...", alongside subtitleoverlay, Klass "Video/Overlay/Subtitle").
// has_core is true if the plugin has at least one element whose Klass is neither Audio- nor Video-classified - this is what keeps a plugin like libgstplayback.so (essential regardless of mode) from being excluded from Audio mode just because one of its several elements happens to touch video.
struct PluginClassification {
  bool has_core = false;
  bool has_audio = false;
  bool has_video = false;
};

// Single full gst-inspect-1.0 -a pass, classifying every element's plugin by that element's Klass.
// Doing this in one pass over every element (rather than one pass per Klass filter) is both cheaper and, more importantly, correct: it lets a plugin accumulate has_core/has_audio/has_video independently instead of only ever learning "this plugin has a Video-classified element somewhere" without also learning it has non-Video elements too.
QHash<QString, PluginClassification> ClassifyPlugins() {

  QHash<QString, PluginClassification> result;

  QProcess process;
  process.start(u"gst-inspect-1.0"_s, {u"-a"_s, u"--no-colors"_s});
  if (!process.waitForFinished(60000)) return result;

  const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split(u'\n');
  QString current_klass;
  for (const QString &line : lines) {
    const int klass_marker = line.indexOf("Klass"_L1);
    if (klass_marker >= 0) {
      current_klass = line.mid(klass_marker + int(QLatin1String("Klass").size())).trimmed();
      continue;
    }

    const int filename_marker = line.indexOf("Filename"_L1);
    if (filename_marker < 0) continue;
    const QString path = line.mid(filename_marker + int(QLatin1String("Filename").size())).trimmed();
    if (path.isEmpty()) continue;

    PluginClassification &classification = result[QFileInfo(path).fileName()];
    const bool is_audio = current_klass.contains("Audio"_L1);
    const bool is_video = current_klass.contains("Video"_L1);
    if (is_audio) classification.has_audio = true;
    if (is_video) classification.has_video = true;
    if (!is_audio && !is_video) classification.has_core = true;
  }

  return result;

}

// Resolves a GStreamer plugin name (e.g. "dvdsub", as gst-inspect-1.0 itself knows it - not a filename) to the actual file it's bundled in.
// Returns an empty string if `plugin_name` doesn't name a real plugin.
QString ResolvePluginFile(const QString &plugin_name) {

  QProcess process;
  process.start(u"gst-inspect-1.0"_s, {plugin_name, u"--no-colors"_s});
  if (!process.waitForFinished(15000) || process.exitCode() != 0) return QString();

  const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split(u'\n');
  for (const QString &line : lines) {
    const int marker = line.indexOf("Filename"_L1);
    if (marker < 0) continue;
    const QString path = line.mid(marker + int(QLatin1String("Filename").size())).trimmed();
    if (!path.isEmpty()) return path;
  }

  return QString();

}

// Classifies each plugin file in `plugin_dir` by whether gst-inspect-1.0 reports it as containing an Audio- or Video-classified element, then returns just the ones `plugin_set` calls for.
// Returns an empty list and sets *error_message only on unrecoverable failure (an empty list because `plugin_set` genuinely matched nothing is not an error).
QStringList SelectPlugins(const QString &plugin_dir, GStreamerDeploy::PluginSet plugin_set, const QStringList &plugin_names, QString &error_message) {

  const QStringList all_plugin_files = FindPluginFiles(plugin_dir);
  if (plugin_set == GStreamerDeploy::PluginSet::All) return all_plugin_files;

  if (QStandardPaths::findExecutable(u"gst-inspect-1.0"_s).isEmpty()) {
    error_message = u"gst-inspect-1.0 is required to select specific GStreamer plugins (pass --gstreamer-plugins=all to bundle everything without it)"_s;
    return QStringList();
  }

  if (plugin_set == GStreamerDeploy::PluginSet::List) {
    QStringList selected;
    for (const QString &plugin_name : plugin_names) {
      const QString file = ResolvePluginFile(plugin_name);
      if (file.isEmpty()) {
        error_message = u"Could not find GStreamer plugin named '%1'"_s.arg(plugin_name);
        return QStringList();
      }
      if (!selected.contains(file)) selected << file;
    }
    qInfo() << "Selected" << selected.size() << "explicitly named GStreamer plugins";
    return selected;
  }

  qInfo() << "Querying gst-inspect-1.0 for plugin classification...";
  const QHash<QString, PluginClassification> classifications = ClassifyPlugins();

  // A plugin gst-inspect-1.0 -a never mentions at all (e.g. libgsttypefindfunctions.so, which provides only GstTypeFindFactory entries - a factory type -a doesn't enumerate, unlike regular elements or even device providers) has no entry in `classifications` to look up.
  // Defaulting an unclassifiable plugin to has_core so it's kept is the conservative choice: staying silently unclassified must not silently drop it from every mode but All.
  static const PluginClassification kUnclassifiedIsCore{true, false, false};

  QStringList selected;
  for (const QString &plugin_file : all_plugin_files) {
    const PluginClassification classification = classifications.value(QFileInfo(plugin_file).fileName(), kUnclassifiedIsCore);

    bool include = false;
    switch (plugin_set) {
      case GStreamerDeploy::PluginSet::Core:
        include = classification.has_core;
        break;
      case GStreamerDeploy::PluginSet::Audio:
        include = classification.has_core || classification.has_audio;
        break;
      case GStreamerDeploy::PluginSet::Video:
        include = classification.has_core || classification.has_video;
        break;
      case GStreamerDeploy::PluginSet::All:
      case GStreamerDeploy::PluginSet::List:
        include = true;  // Unreachable: both are handled by an early return above.
        break;
    }

    if (include) selected << plugin_file;
  }

  qInfo() << "Selected" << selected.size() << "of" << all_plugin_files.size() << "GStreamer plugins";

  return selected;

}

// gst-plugin-scanner's directory and even its filename (e.g. Arch/openSUSE ship it as "gst-plugin-scanner-x86_64", not plain "gst-plugin-scanner") vary across distributions, so neither can be hardcoded.
// Ask pkg-config for the authoritative directory first (it's queried on the very machine whose GStreamer is being bundled, so it reflects that distro's actual layout), then fall back to a prefix search over commonly-used locations.
QString FindGstPluginScanner() {

  QProcess pkg_config;
  pkg_config.start(u"pkg-config"_s, {u"--variable=pluginscannerdir"_s, u"gstreamer-1.0"_s});
  if (pkg_config.waitForFinished(5000) && pkg_config.exitCode() == 0) {
    const QString dir = QString::fromUtf8(pkg_config.readAllStandardOutput()).trimmed();
    if (!dir.isEmpty()) {
      const QStringList found = Utilities::FilesWithPrefix(dir, u"gst-plugin-scanner"_s);
      if (!found.isEmpty()) return found.first();
    }
  }

  static const QStringList kCandidateDirs = {
      u"/usr/libexec/gstreamer-1.0"_s,
      u"/usr/lib64/gstreamer-1.0"_s,
      u"/usr/lib/gstreamer-1.0"_s,
      u"/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0"_s,
      u"/usr/lib/gstreamer1.0/gstreamer-1.0"_s,
  };
  for (const QString &dir : kCandidateDirs) {
    const QStringList found = Utilities::FilesWithPrefix(dir, u"gst-plugin-scanner"_s);
    if (!found.isEmpty()) return found.first();
  }

  return QString();

}

// libgstsoup.so dlopen()s libsoup-3.0.so.0 rather than linking it as a normal DT_NEEDED dependency - readelf -d shows no NEEDED entry for it at all, despite the plugin referencing soup_message_* symbols and importing dlopen itself (confirmed via strace: it tries and fails to open libsoup-3.0.so.0 at runtime) - so DependencyWalker's ordinary DT_NEEDED-based walk never discovers or bundles it.
// Left unhandled, souphttpsrc/souphttpclientsink still register successfully (registration is metadata-only and doesn't need libsoup loaded), but any attempt to actually create/use them fails, which decodebin then surfaces as "Missing GStreamer plugin for HTTPS protocol source" - a working-looking but non-functional plugin, not an absent one.
void HandleSoupDlopenDependency(DependencyWalker &dependency_walker) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libgstsoup.so"_L1)) continue;

    qInfo() << "Bundling libsoup (dlopen'd by the GStreamer soup plugin, not a normal linked dependency)...";
    const QString libsoup = dependency_walker.FindLibrary(u"libsoup-3.0.so.0"_s);
    if (libsoup.isEmpty()) {
      qWarning() << "Could not find libsoup-3.0.so.0; HTTPS/HTTP playback via the bundled GStreamer soup plugin will likely fail at runtime despite appearing to load";
    }
    else {
      dependency_walker.AddElfTree(libsoup);
    }

    break;
  }

}

}  // namespace

namespace GStreamerDeploy {

bool Deploy(DependencyWalker &dependency_walker, PluginSet plugin_set, const QStringList &plugin_names, QString &error_message) {

  for (const QString &lib : dependency_walker.all_elfs()) {
    if (!QFileInfo(lib).fileName().startsWith("libgstreamer-1.0"_L1)) continue;

    qInfo() << "Bundling GStreamer 1.0 directory (for GST_PLUGIN_PATH)...";
    const QStringList locations = dependency_walker.FindWithPrefixInLibraryLocations(u"gstreamer-1.0"_s);
    if (locations.isEmpty()) {
      error_message = u"Could not find GStreamer 1.0 directory"_s;
      return false;
    }

    const QStringList plugin_files = SelectPlugins(locations.first(), plugin_set, plugin_names, error_message);
    if (plugin_files.isEmpty() && !error_message.isEmpty()) return false;

    qInfo() << "Bundling dependencies of GStreamer 1.0 directory...";
    for (const QString &plugin_file : plugin_files) {
      dependency_walker.AddElfTree(plugin_file);
    }

    HandleSoupDlopenDependency(dependency_walker);

    qInfo() << "Determining gst-plugin-scanner...";
    const QString scanner = FindGstPluginScanner();
    if (scanner.isEmpty()) {
      qWarning() << "Could not find gst-plugin-scanner; external GStreamer plugins (e.g. some codecs) may fail to load in the AppImage.";
    }
    else {
      dependency_walker.AddElfTree(scanner);
    }

    break;
  }

  return true;

}

}  // namespace GStreamerDeploy
