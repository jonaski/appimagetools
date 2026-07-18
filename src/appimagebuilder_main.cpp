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

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QDebug>

#include "logging.h"
#include "utilities.h"
#include "appimagebuilder.h"

using namespace Qt::Literals::StringLiterals;

int main(int argc, char *argv[]) {

  Logging::InstallHandler();

  LIEF::logging::disable();

  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(u"appimagebuilder"_s);
  QCoreApplication::setApplicationVersion(u"0.1.1"_s);

  QCommandLineParser parser;
  parser.setApplicationDescription(u"Turns an already-deployed AppDir into a .AppImage file"_s);
  parser.addHelpOption();
  parser.addPositionalArgument(u"appdir"_s, u"Path to the AppDir to package"_s);

  const QCommandLineOption version_option(u"version"_s, u"Version string to stamp into X-AppImage-Version and use in the output filename; if not given, detected by running the AppDir's main executable (from Exec= in its .desktop file) with --version"_s, u"version"_s);
  parser.addOption(version_option);

  parser.process(app);

  const QStringList positional = parser.positionalArguments();
  if (positional.size() != 1) {
    qCritical() << "Please specify the path to the AppDir";
    return 1;
  }

  const QFileInfo app_dir_info(positional.first());
  if (!app_dir_info.exists()) {
    qCritical() << "The specified path does not exist";
    return 1;
  }
  if (!app_dir_info.isDir()) {
    qCritical().noquote() << "Supplied argument is not a directory.\n"
                              "To extract an AppImage, run it with --appimage-extract instead.";
    return 1;
  }

  QString error_message;
  if (!Utilities::CheckMksquashfsVersion(error_message)) {
    qCritical().noquote() << "ERROR:" << error_message;
    return 1;
  }

  AppImageBuilder::Options options;
  options.version = parser.value(version_option);
  QString output_path;
  if (!AppImageBuilder::Build(app_dir_info.canonicalFilePath(), options, output_path, error_message)) {
    qCritical().noquote() << "ERROR:" << error_message;
    return 1;
  }

  return 0;

}
