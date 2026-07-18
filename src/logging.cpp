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

#include "logging.h"

#include <cstdio>
#include <cstdlib>

#include <QtGlobal>
#include <QString>
#include <QMessageLogContext>

namespace {

void LogToStreams(QtMsgType type, const QMessageLogContext &message_log_context, const QString &msg) {

  Q_UNUSED(message_log_context)

  switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
      std::fprintf(stdout, "%s\n", qPrintable(msg));
      break;
    case QtWarningMsg:
    case QtCriticalMsg:
      std::fprintf(stderr, "%s\n", qPrintable(msg));
      break;
    case QtFatalMsg:
      std::fprintf(stderr, "%s\n", qPrintable(msg));
      std::fflush(stderr);
      std::abort();
  }

}

}  // namespace

namespace Logging {

void InstallHandler() {
  qInstallMessageHandler(LogToStreams);
}

}  // namespace Logging
