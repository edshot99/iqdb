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

#include <iqdb/debug.h>
#include <iqdb/server.h>
#include <iqdb/sqlite_db.h>

using namespace iqdb;

int main(int argc, char **argv) {
  try {
    // open_swap();
    if (argc < 2)
      help();

    if (!strncmp(argv[1], "-d=", 3)) {
      debug_level = std::stoi(argv[1] + 3);
      INFO("Debug level set to {}\n", debug_level);
      argv++;
      argc--;
    }

    if (!strcasecmp(argv[1], "http")) {
      const std::string host = argc > 2 ? argv[2] : "localhost";
      const int port = argc > 3 ? std::stoi(argv[3]) : 8000;
      const std::string filename = argc > 4 ? argv[4] : "iqdb.db";

      http_server(host, port, filename);
    } else {
      help();
    }
  } catch (const iqdb::base_error &err) {
    INFO("Error: {}.\n", err.what());
    if (errno)
      perror("Last system error");
  }

  return 0;
}
