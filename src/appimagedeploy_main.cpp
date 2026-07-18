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

#include <LIEF/logging.hpp>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QDebug>

#include "appimagedeploy.h"
#include "logging.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[]) {

  Logging::InstallHandler();

  // Silences LIEF's own internal parser logging - most audibly "Failed to parse note", which it prints for any PT_NOTE entry it doesn't fully recognize (e.g. distro-added .note.package metadata, or newer .note.gnu.property CET/IBT markers).
  // Harmless noise for us: ElfFile only ever reads DT_NEEDED, PT_INTERP, machine type, and DT_RUNPATH/DT_RPATH, none of which come from NOTE segments.
  LIEF::logging::disable();

  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(u"appimagedeploy"_s);
  QCoreApplication::setApplicationVersion(u"0.1.1"_s);

  QCommandLineParser command_line_parser;
  command_line_parser.setApplicationDescription(u"Turns a PREFIX directory into an AppDir by deploying its dependencies and writing AppRun"_s);
  command_line_parser.addHelpOption();

  const QCommandLineOption preserve_cwd_option(u"preserve-cwd"_s, u"Preserve the current working directory when running the app"_s);
  const QCommandLineOption exclude_corelibs_option({u"s"_s, u"use-exclude-list"_s}, u"Use exclude list"_s);
  const QCommandLineOption gstreamer_plugins_option(u"gstreamer-plugins"_s, u"Which GStreamer plugins to bundle: core, audio, video, all, or list"_s, u"set"_s, u"all"_s);
  const QCommandLineOption gstreamer_plugin_list_option(u"gstreamer-plugin-list"_s, u"Comma-separated GStreamer plugin names to bundle exactly (e.g. coreelements,playback,typefindfunctions,audioconvert); only used when --gstreamer-plugins=list"_s, u"names"_s);
  const QCommandLineOption qt_sql_plugins_option(u"qt-sql-plugins"_s, u"Which Qt SQL drivers to bundle: 'none', 'all', or a comma-separated list of driver names (e.g. sqlite,mysql,psql)"_s, u"value"_s, u"all"_s);
  command_line_parser.addOption(exclude_corelibs_option);
  command_line_parser.addOption(preserve_cwd_option);
  command_line_parser.addOption(gstreamer_plugins_option);
  command_line_parser.addOption(gstreamer_plugin_list_option);
  command_line_parser.addOption(qt_sql_plugins_option);
  command_line_parser.addPositionalArgument(u"desktopfile"_s, u"Path to the .desktop file inside the AppDir to deploy (<AppDir>/usr/share/applications/foo.desktop)"_s);

  command_line_parser.process(app);

  const QStringList positional = command_line_parser.positionalArguments();
  if (positional.size() != 1) {
    qCritical().noquote() << "Please supply the path to a desktop file in an FHS-like AppDir, e.g.:\n"
                             "  appimagedeploy AppDir/usr/share/applications/myapp.desktop";
    return 1;
  }

  if (QStandardPaths::findExecutable(u"patchelf"_s).isEmpty()) {
    qCritical() << "Required helper tool 'patchelf' is missing";
    return 1;
  }

  const QString gstreamer_plugins_value = command_line_parser.value(gstreamer_plugins_option).trimmed().toLower();
  GStreamerDeploy::PluginSet gstreamer_plugin_set;
  if (gstreamer_plugins_value == "core"_L1) {
    gstreamer_plugin_set = GStreamerDeploy::PluginSet::Core;
  }
  else if (gstreamer_plugins_value == "audio"_L1) {
    gstreamer_plugin_set = GStreamerDeploy::PluginSet::Audio;
  }
  else if (gstreamer_plugins_value == "video"_L1) {
    gstreamer_plugin_set = GStreamerDeploy::PluginSet::Video;
  }
  else if (gstreamer_plugins_value == "all"_L1) {
    gstreamer_plugin_set = GStreamerDeploy::PluginSet::All;
  }
  else if (gstreamer_plugins_value == "list"_L1) {
    gstreamer_plugin_set = GStreamerDeploy::PluginSet::List;
  }
  else {
    qCritical().noquote() << "Invalid value for --gstreamer-plugins:" << gstreamer_plugins_value << "(expected core, audio, video, all, or list)";
    return 1;
  }

  QStringList gstreamer_plugin_names;
  if (gstreamer_plugin_set == GStreamerDeploy::PluginSet::List) {
    if (!command_line_parser.isSet(gstreamer_plugin_list_option)) {
      qCritical().noquote() << "--gstreamer-plugins=list requires --gstreamer-plugin-list=<comma-separated names>";
      return 1;
    }
    const QStringList raw_names = command_line_parser.value(gstreamer_plugin_list_option).split(u',');
    for (const QString &raw_name : raw_names) {
      const QString name = raw_name.trimmed();
      if (!name.isEmpty()) gstreamer_plugin_names << name;
    }
    if (gstreamer_plugin_names.isEmpty()) {
      qCritical().noquote() << "--gstreamer-plugin-list did not contain any plugin names";
      return 1;
    }
  }

  const QString qt_sql_plugins_value = command_line_parser.value(qt_sql_plugins_option).trimmed();
  QtDeploy::SqlPluginSet qt_sql_plugin_set;
  QStringList qt_sql_plugin_names;
  if (qt_sql_plugins_value.compare("all"_L1, Qt::CaseInsensitive) == 0) {
    qt_sql_plugin_set = QtDeploy::SqlPluginSet::All;
  }
  else if (qt_sql_plugins_value.compare("none"_L1, Qt::CaseInsensitive) == 0) {
    qt_sql_plugin_set = QtDeploy::SqlPluginSet::None;
  }
  else {
    qt_sql_plugin_set = QtDeploy::SqlPluginSet::List;
    const QStringList raw_names = qt_sql_plugins_value.split(u',');
    for (const QString &raw_name : raw_names) {
      const QString name = raw_name.trimmed();
      if (!name.isEmpty()) qt_sql_plugin_names << name;
    }
    if (qt_sql_plugin_names.isEmpty()) {
      qCritical().noquote() << "Invalid value for --qt-sql-plugins:" << qt_sql_plugins_value << "(expected none, all, or a comma-separated list of driver names)";
      return 1;
    }
  }

  AppImageDeployOptions options;
  if (command_line_parser.isSet(preserve_cwd_option)) {
    options |= AppImageDeployOption::PreserveCwd;
  }
  if (command_line_parser.isSet(exclude_corelibs_option)) {
    options |= AppImageDeployOption::UseExcludeList;
  }

  QString error_message;
  if (!AppImageDeploy::Run(positional.first(), options, gstreamer_plugin_set, gstreamer_plugin_names, qt_sql_plugin_set, qt_sql_plugin_names, error_message)) {
    qCritical().noquote() << "ERROR:" << error_message;
    return 1;
  }

  return 0;

}
