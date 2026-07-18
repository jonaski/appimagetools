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

#ifndef LOGGING_H
#define LOGGING_H

// Shared between the appimagedeploy and appimagebuilder executables.
namespace Logging {

// Installs a qDebug/qInfo/qWarning/qCritical handler that writes straight to stdout/stderr. Some Qt builds (this repo has been developed against one) route these to the systemd journal instead whenever stderr isn't a real console (e.g. under CI runners, containers, or non-interactive automation); a command-line tool's log output has to actually reach the invoking process's stdout/stderr, so this installs an explicit handler instead of relying on Qt's console detection.
void InstallHandler();

}  // namespace Logging

#endif  // LOGGING_H
