/* 
 * Olive. Olive is a free non-linear video editor for Windows, macOS, and Linux.
 * Copyright (C) 2018  {{ organization }}
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
 *along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "ui/mainwindow.h"
#include <QApplication>
#include <iostream>
#include <QLoggingCategory>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "project/effect.h"
#include "panels/panelmanager.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
}

constexpr auto APP_NAME = "Chestnut";

int main(int argc, char *argv[])
{
  auto launch_fullscreen = false;
  QString load_proj;
  int c;

  while ((c = getopt(argc, argv, "fhi:l:s")) != -1)
  {
    switch (c) {
      case 'f':
        // fullscreen
        launch_fullscreen = true;
        break;
      case 'h':
        printf("Usage: %s [options] \n\nOptions:"
               "\n\t-f \t\tStart in full screen mode"
               "\n\t-h \t\tShow this help and exit"
               "\n\t-i <filename> \tLoad a project file"
               "\n\t-l <level> \t\tSet the logging level (fatal, critical, warning, info, debug)"
               "\n\t-s  \t\tDisable shaders\n\n",
               argv[0]);
        return 0;
      case 'i':
        // input file to load
        load_proj = optarg;
        break;
      case 'l':
        // log level
        {
          QString val = optarg;
          if (val == "fatal") {
            chestnut::debug::debug_level = QtMsgType::QtFatalMsg;
          } else if (val == "critical") {
            chestnut::debug::debug_level = QtMsgType::QtCriticalMsg;
          } else if (val == "warning") {
            chestnut::debug::debug_level = QtMsgType::QtWarningMsg;
          } else if (val == "info") {
            chestnut::debug::debug_level = QtMsgType::QtInfoMsg;
          } else if (val == "debug") {
            chestnut::debug::debug_level = QtMsgType::QtDebugMsg;
          } else {
            std::cerr << "Unknown logging level:" << val.toUtf8().toStdString() << std::endl;
          }
        }
        break;
      case 's':
        // Disable shader effects
        shaders_are_enabled = false;
        break;
      default:
        std::cerr << "Ignoring:" << c << std::endl;
        break;
    }
  }

  qInstallMessageHandler(debug_message_handler);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
  // init ffmpeg subsystem
  av_register_all();
#endif

#if LIBAVFILTER_VERSION_INT < AV_VERSION_INT(7, 14, 100)
  avfilter_register_all();
#endif

  QApplication a(argc, argv);
  QApplication::setWindowIcon(QIcon(":/icons/chestnut.png"));

  const QString name(APP_NAME);
  MainWindow& w = MainWindow::instance(nullptr, name);
  w.initialise();
  w.updateTitle("");

  if (!load_proj.isEmpty()) {
    w.launch_with_project(load_proj);
  }
  if (launch_fullscreen) {
    w.showFullScreen();
  } else {
    w.showMaximized();
  }

  int retCode = QApplication::exec();

  MainWindow::tearDown();
  return retCode;
}
