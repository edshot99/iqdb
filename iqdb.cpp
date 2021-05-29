/***************************************************************************\
    iqdb.cpp - iqdb server (database maintenance and queries)

    Copyright (C) 2008 piespy@gmail.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\**************************************************************************/

#include <cstring>
#include <cstdlib>
#include <string>
#include <csignal>

#define DEBUG_IQDB
#include "debug.h"
#include "imgdb.h"
#include "server.h"

int debug_level = DEBUG_errors | DEBUG_base | DEBUG_summary | DEBUG_connections | DEBUG_images | DEBUG_imgdb; // | DEBUG_dupe_finder; // | DEBUG_resizer;

int main(int argc, char **argv) {
  try {
    //	open_swap();
    if (argc < 2)
      help();

    if (!strncmp(argv[1], "-d=", 3)) {
      debug_level = strtol(argv[1] + 3, NULL, 0);
      DEBUG(base)("Debug level set to %x\n", debug_level);
      argv++;
      argc--;
    }

    if (!strcasecmp(argv[1], "http")) {
      const std::string host = argc >= 2 ? argv[2] : "localhost";
      const int port = argc >= 3 ? std::stoi(argv[3]) : 8000;
      const std::string filename = argc >= 4 ? argv[4] : "iqdb.db";

      http_server(host, port, filename);
    } else {
      help();
    }

    // Handle this specially because it means we need to fix the DB before restarting :(
  } catch (const imgdb::data_error &err) {
    DEBUG(errors)("Data error: %s.\n", err.what());
    exit(10);

  } catch (const imgdb::base_error &err) {
    DEBUG(errors)("Caught error %s: %s.\n", err.type(), err.what());
    if (errno)
      perror("Last system error");
  }

  return 0;
}
