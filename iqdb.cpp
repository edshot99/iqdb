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

    const char *filename = argv[2]; int flags = 0;

    if (!strcasecmp(argv[1], "add")) {
      add(filename);
    } else if (!strcasecmp(argv[1], "list")) {
      list(filename);
    } else if (!strncasecmp(argv[1], "query", 5)) {
      if (argv[1][5] == 'u')
        flags |= imgdb::dbSpace::flag_uniqueset;

      const char *img = argv[3];
      int numres = argc < 6 ? -1 : strtol(argv[4], NULL, 0);
      if (numres < 1)
        numres = 16;
      query(filename, img, numres, flags);
    } else if (!strcasecmp(argv[1], "sim")) {
      imgdb::imageId id = strtoll(argv[3], NULL, 0);
      int numres = argc < 6 ? -1 : strtol(argv[4], NULL, 0);
      if (numres < 1)
        numres = 16;
      sim(filename, id, numres);
    } else if (!strcasecmp(argv[1], "rehash")) {
      rehash(filename);
    } else if (!strcasecmp(argv[1], "command")) {
      command(argc - 2, argv + 2);
    } else if (!strcasecmp(argv[1], "listen")) {
      server(argv[2], argc - 3, argv + 3, false);
    } else if (!strcasecmp(argv[1], "listen2")) {
      server(argv[2], argc - 3, argv + 3, true);
    } else if (!strcasecmp(argv[1], "http")) {
      http_server("localhost", 8000, "iqdb.db");
    } else if (!strcasecmp(argv[1], "statistics")) {
      stats(filename);
    } else if (!strcasecmp(argv[1], "count")) {
      count(filename);
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
