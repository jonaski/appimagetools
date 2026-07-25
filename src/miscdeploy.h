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

#ifndef MISCDEPLOY_H
#define MISCDEPLOY_H

class QString;

class AppDir;
class DependencyWalker;

namespace MiscDeploy {

// Compiles usr/share/glib-2.0/schemas if present and not already compiled (AppRun exports GSETTINGS_SCHEMA_DIR so the bundled copy gets picked up).
// Returns false and sets error_message on failure.
bool HandleGlibSchemas(const AppDir &appdir, QString &error_message);

// Adds an etc/fonts/fonts.conf symlink to the system fontconfig if the AppDir doesn't already ship its own.
bool DeployFontconfig(const AppDir &appdir, QString &error_message);

// Bundles Gdk pixbuf loaders (and patches their loaders.cache to not reference the build-time path) if libgdk_pixbuf is a dependency.
// Returns false and sets error_message on failure.
bool HandleGdk(DependencyWalker &dependency_walker, const AppDir &appdir, QString &error_message);

// Bundles the Gtk <gtk_version> module directory, default theme (Gtk <= 3), and immodules.cache (Gtk <= 3, patched like loaders.cache) if libgtk-<gtk_version> is a dependency.
// Returns false and sets error_message on failure.
bool DeployGtkDirectory(DependencyWalker &dependency_walker, const AppDir &appdir, const int gtk_version, QString &error_message);

// Removes libasound.so* from the dependency set if present, so ALSA is always resolved against the running system's alsa-lib rather than bundled: its plugin modules, /usr/share/alsa config, and hardware UCM profiles are versioned together by the host and bundling them breaks device access on any host whose alsa-lib/UCM data doesn't exactly match the build host's (see miscdeploy.cpp for the failure mode this avoids).
void HandleAlsa(DependencyWalker &dependency_walker);

// Bundles the gio/modules directory (and its giomodule.cache, which unlike loaders.cache/immodules.cache holds no absolute paths so needs no patching) if libgio-2.0 is a dependency, so bundled GIO can still load its extra modules (gsettings backends, TLS backend, proxy resolvers, volume monitors) via GIO_EXTRA_MODULES. Best-effort: only warns if not found.
void HandleGioModules(DependencyWalker &dependency_walker, const AppDir &appdir);

// Bundles the build host's system-wide GnuTLS priority/crypto-policy config file (resolving the usual /etc/crypto-policies/back-ends/gnutls.config symlink to its real target) if libgnutls is a dependency, so AppRun can point GNUTLS_SYSTEM_PRIORITY_FILE at it. Without this, the bundled GnuTLS - which has that host-specific path baked in as its compiled-in default - fails outright with "Failed to set GnuTLS session priority with error beginning at %COMPAT" on any host that doesn't happen to use the same crypto-policies mechanism (e.g. Debian/Ubuntu, which don't ship it at all). Best-effort: only warns if not found.
//
// Known remaining limitation: GnuTLS's system trust store (loaded via the bundled p11-kit-trust.so PKCS#11 module) still reports "System trust contains zero trusted certificates" on hosts that don't share this exact openSUSE/Fedora-style /etc/pki/trust layout (e.g. Debian/Ubuntu, which compiles p11-kit with different, incompatible trust paths). There is no environment-variable override for this, unlike the priority file above; fixing it properly would require either bundling a portable, file-based GnuTLS build instead of the system one, or a persistent (outside-the-AppDir) system change - not attempted here.
void HandleGnuTls(DependencyWalker &dependency_walker, const AppDir &appdir);

// Bundles the pulseaudio plugin directory if libpulse is a dependency.
void HandlePulseAudio(DependencyWalker &dependency_walker);

// Refuses to continue if any collected ELF is an Nvidia driver library: bundling libnvidia* alongside the system's libGL reliably segfaults.
// Returns false and sets error_message if found.
bool HandleNvidia(const DependencyWalker &dependency_walker, QString &error_message);

}  // namespace MiscDeploy

#endif  // MISCDEPLOY_H
