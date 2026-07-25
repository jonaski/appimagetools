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

#ifndef APPIMAGEDEPLOY_H
#define APPIMAGEDEPLOY_H

#include <QString>
#include <QStringList>

#include "appimagedeployoptions.h"
#include "gstreamerdeploy.h"
#include "qtdeploy.h"

// Top-level orchestration for the appimagedeploy tool: turns a prefix/ directory into a self-contained AppDir by discovering every ELF dependency, bundling desktop-integration support files (Gdk, Gtk, GStreamer, PulseAudio, GLib schemas, fontconfig), Qt plugins/QML, the ELF interpreter, writing AppRun, copying and rpath-patching every dependency, and copying in copyright files. ALSA is deliberately excluded rather than bundled (see MiscDeploy::HandleAlsa).
namespace AppImageDeploy {

// `desktop_file_path` is expected at <AppDir>/usr/share/applications/foo.desktop, as accepted by AppDir::Create(). `gstreamer_plugin_names` is only consulted when `gstreamer_plugin_set` is GStreamerDeploy::PluginSet::List, and `qt_sql_plugin_names` only when `qt_sql_plugin_set` is QtDeploy::SqlPluginSet::List. Returns false and sets *error_message on any unrecoverable failure.
bool Run(const QString &desktop_file_path, const AppImageDeployOptions &options, GStreamerDeploy::PluginSet gstreamer_plugin_set, const QStringList &gstreamer_plugin_names, QtDeploy::SqlPluginSet qt_sql_plugin_set, const QStringList &qt_sql_plugin_names, QString &error_message);

}  // namespace AppImageDeploy

#endif  // APPIMAGEDEPLOY_H
