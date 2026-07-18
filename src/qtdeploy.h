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

#ifndef QTDEPLOY_H
#define QTDEPLOY_H

#include <QString>
#include <QStringList>

class QtDeployInfo;
class AppDir;
class DependencyWalker;

namespace QtDeploy {

constexpr int kQtVersion = 6;

// Which Qt SQL drivers (plugins/sqldrivers/) to bundle, if the app depends on Qt SQL at all.
enum class SqlPluginSet {
  All,   // Every driver found (default, matches the pre-existing behavior).
  None,  // Don't bundle plugins/sqldrivers/ at all.
  List,  // Only the drivers named in Deploy()'s `sql_plugin_names`, e.g. "sqlite", "mysql", "psql", "odbc", "oci", "db2", "ibase", "mimer".
};

// True if walker.all_elfs() contains "libQt<kQtVersion>Core.so.<kQtVersion>".
bool IsQtDependency(const DependencyWalker &dependency_walker);

// Adds every Qt plugin (and, if present, QML module) implied by what's already in walker.all_elfs() to `walker`. `sql_plugin_names` is only consulted when `sql_plugin_set` is SqlPluginSet::List.
// Returns false and sets *error_message on unrecoverable failure (e.g. libQt<kQtVersion>Core.so.<kQtVersion> not found, the platforms/libqxcb.so plugin missing, or a named SQL driver in `sql_plugin_names` not found).
bool Deploy(DependencyWalker &dependency_walker, const AppDir &appdir, QtDeployInfo &info, SqlPluginSet sql_plugin_set, const QStringList &sql_plugin_names, QString &error_message);

}  // namespace QtDeploy

#endif  // QTDEPLOY_H
