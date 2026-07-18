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

// Result of QtDeploy::Deploy(), needed later by AppImageDeploy when patching qt_prfxpath into the deployed libQt<QtDeploy::kQtVersion>Core.so.<QtDeploy::kQtVersion>.
struct QtDeployInfo {
  QString qt_prefix_path;

  // True if qt_prefix_path had to be *guessed* (by locating libqxcb.so) because the qt_prfxpath string embedded in the Qt Core library pointed somewhere that turned out not to contain a plugins/ directory (seen on e.g. Ubuntu/Alpine, where qt_prfxpath=/usr but plugins actually live under /usr/lib/qt<N>).
  // When true, AppImageDeploy patches qt_prfxpath to ".." instead of a computed relative path.
  bool quirks_mode_patch_qt_prfxpath = false;
};
