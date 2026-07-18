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

#ifndef GSTREAMERDEPLOY_H
#define GSTREAMERDEPLOY_H

#include <QString>
#include <QStringList>

class DependencyWalker;

// If the app depends on libgstreamer-1.0, bundles the requested subset of the gstreamer-1.0 plugin directory (so GST_PLUGIN_PATH in AppRun finds it) plus gst-plugin-scanner.
namespace GStreamerDeploy {

// Which subset of the build host's GStreamer plugins to bundle.
// Classification (for Core/Audio/Video) is done by asking gst-inspect-1.0 which plugins have at least one element classified Audio or Video; everything else - core infrastructure (coreelements, playback, typefindfunctions, ...) and format-agnostic container demuxers/muxers/parsers (matroska, mp4, ogg, ...), which GStreamer doesn't tag Audio or Video since they carry whichever stream type is actually inside - is treated as Core and always included regardless of which of these is selected,
// since Audio-only or Video-only mode still needs them to unpack real, containerized files.
//
// This classification is inherently approximate: GStreamer's own Klass metadata sometimes disagrees with what's actually only useful in an audio or video context (e.g. dvdsub's subtitle parser isn't classified Video at all, and videoframe_audiolevel's analyzer is classified Audio despite needing a video frame to operate on).
// List exists as an exact, manual escape hatch for exactly these cases.
enum class PluginSet {
  Core,   // Only core/generic plugins - no Audio- or Video-classified codecs at all.
  Audio,  // Core plugins plus anything classified Audio.
  Video,  // Core plugins plus anything classified Video.
  All,    // Every plugin found, unfiltered.
  List,   // Exactly the plugins named in Deploy()'s `plugin_names`, nothing more, nothing less.
};

// `plugin_names` is only consulted when `plugin_set` is List: each entry is a GStreamer plugin name (as gst-inspect-1.0 itself knows it, e.g. "coreelements", "audioconvert", "dvdsub" - not a filename), resolved to its actual file via gst-inspect-1.0.
// Ignored for every other PluginSet value.
bool Deploy(DependencyWalker &dependency_walker, PluginSet plugin_set, const QStringList &plugin_names, QString &error_message);

}  // namespace GStreamerDeploy

#endif  // GSTREAMERDEPLOY_H
