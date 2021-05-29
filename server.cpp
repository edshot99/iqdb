/***************************************************************************\
    server.cpp - iqdb server (database maintenance and queries)

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

#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef MEMCHECK
#include <malloc.h>
#include <mcheck.h>
#endif

#include <algorithm>
#include <list>
#include <vector>

#include "auto_clean.h"
#define DEBUG_IQDB
#include "debug.h"
#include "imgdb.h"

#include "vendor/httplib.h"
#include "vendor/json.hpp"

using nlohmann::json;

extern int debug_level;

static void die(const char fmt[], ...) __attribute__((format(printf, 1, 2))) __attribute__((noreturn));
static void die(const char fmt[], ...) {
  va_list args;

  va_start(args, fmt);
  fflush(stdout);
  vfprintf(stderr, fmt, args);
  va_end(args);

  exit(1);
}

class dbSpaceAuto : public AutoCleanPtr<imgdb::dbSpace> {
public:
  dbSpaceAuto(){};
  dbSpaceAuto(const char *filename, int mode) : AutoCleanPtr<imgdb::dbSpace>(loaddb(filename, mode)), m_filename(filename){};
  dbSpaceAuto(const dbSpaceAuto &other) {
    if (other != NULL)
      throw imgdb::internal_error("Can't copy-construct dbSpaceAuto.");
  }

  void save() { (*this)->save_file(m_filename.c_str()); }
  void load(const char *filename, int mode) {
    this->set(loaddb(filename, mode));
    m_filename = filename;
  }
  void clear() { this->set(NULL); }

  const std::string &filename() const { return m_filename; }

private:
  static imgdb::dbSpace *loaddb(const char *fn, int mode) {
    imgdb::dbSpace *db = imgdb::dbSpace::load_file(fn, mode);
    DEBUG(summary)("Database loaded from %s, has %zd images.\n", fn, db->getImgCount());
    return db;
  }

  std::string m_filename;
};

class dbSpaceAutoMap {
  typedef std::list<dbSpaceAuto> list_type;
  typedef std::vector<dbSpaceAuto *> array_type;

public:
  dbSpaceAutoMap(int ndbs, int mode, const char *const *filenames) {
    m_array.reserve(ndbs);
    while (ndbs--)
      (*m_array.insert(m_array.end(), &*m_list.insert(m_list.end(), dbSpaceAuto())))->load(*filenames++, mode);
  }

  dbSpaceAuto &at(unsigned int dbid, bool append = false) {
    while (append && size() <= dbid)
      m_array.insert(m_array.end(), &*m_list.insert(m_list.end(), dbSpaceAuto()));
    if (dbid >= size() || (!append && !*m_array[dbid]))
      throw imgdb::param_error("dbId out of range.");
    return *m_array[dbid];
  }

  dbSpaceAuto &operator[](unsigned int dbid) {
    return *m_array[dbid];
  }

  size_t size() const { return m_array.size(); }

private:
  array_type m_array;
  list_type m_list;
};

#ifdef INTMATH
#define ScD(x) ((double)(x) / imgdb::ScoreMax)
#else
#define ScD(x) (x)
#endif

void add(const char *fn) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_alter);
  while (!feof(stdin)) {
    char fn[1024];
    char line[1024];
    imgdb::imageId id;
    int width = -1, height = -1;
    if (!fgets(line, sizeof(line), stdin)) {
      DEBUG(errors)("Read error.\n");
      continue;
    }
    if (sscanf(line, "%" FMT_imageId " %d %d:%1023[^\r\n]\n", &id, &width, &height, fn) != 4 &&
        sscanf(line, "%" FMT_imageId ":%1023[^\r\n]\n", &id, fn) != 2) {
      DEBUG(errors)("Invalid line %s\n", line);
      continue;
    }
    try {
      if (!db->hasImage(id)) {
        DEBUG(images)("Adding %s = %08" FMT_imageId "...\r", fn, id);
        db->addImage(id, fn);
      }
      if (width != -1 && height != -1)
        db->setImageRes(id, width, height);
    } catch (const imgdb::simple_error &err) {
      DEBUG(errors)("%s: %s %s\n", fn, err.type(), err.what());
    }
  }
  db.save();
}

void list(const char *fn) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_alter);
  imgdb::imageId_list list = db->getImgIdList();
  for (imgdb::imageId_list::iterator itr = list.begin(); itr != list.end(); ++itr)
    printf("%08" FMT_imageId "\n", *itr);
}

void rehash(const char *fn) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_normal);
  db->rehash();
  db.save();
}

void stats(const char *fn) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_simple);
  size_t count = db->getImgCount();
  imgdb::stats_t stats = db->getCoeffStats();
  for (imgdb::stats_t::const_iterator itr = stats.begin(); itr != stats.end(); ++itr) {
    printf("c=%d\ts=%d\ti=%d\t%zd = %zd\n", itr->first >> 24, (itr->first >> 16) & 0xff, itr->first & 0xffff, itr->second, 100 * itr->second / count);
  }
}

void count(const char *fn) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_simple);
  printf("%zd images\n", db->getImgCount());
}

void query(const char *fn, const char *img, int numres, int flags) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_simple);
  imgdb::sim_vector sim = db->queryImg(imgdb::queryArg(img, numres, flags));
  for (size_t i = 0; i < sim.size(); i++)
    printf("%08" FMT_imageId " %lf %d %d\n", sim[i].id, ScD(sim[i].score), sim[i].width, sim[i].height);
}

void sim(const char *fn, imgdb::imageId id, int numres) {
  dbSpaceAuto db(fn, imgdb::dbSpace::mode_readonly);
  imgdb::sim_vector sim = db->queryImg(imgdb::queryArg(db, id, numres, 0));
  for (size_t i = 0; i < sim.size(); i++)
    printf("%08" FMT_imageId " %lf %d %d\n", sim[i].id, ScD(sim[i].score), sim[i].width, sim[i].height);
}

enum event_t { DO_QUITANDSAVE };

#define DB dbs.at(dbid)

std::pair<char *, size_t> read_blob(const char *size_arg, FILE *rd) {
  size_t blob_size = strtoul(size_arg, NULL, 0);
  char *blob = new char[blob_size];
  if (fread(blob, 1, blob_size, rd) != blob_size)
    throw imgdb::param_error("Error reading literal image data");

  return std::make_pair(blob, blob_size);
}

DEFINE_ERROR(command_error, imgdb::param_error)

void do_commands(FILE *rd, FILE *wr, dbSpaceAutoMap &dbs, bool allow_maint) {
  while (!feof(rd))
    try {
      fprintf(wr, "000 iqdb ready\n");
      fflush(wr);

      char command[1024];
      if (!fgets(command, sizeof(command), rd)) {
        if (feof(rd)) {
          fprintf(wr, "100 EOF detected.\n");
          DEBUG(warnings)("End of input\n");
          return;
        } else if (ferror(rd)) {
          fprintf(wr, "300 File error %s\n", strerror(errno));
          DEBUG(errors)("File error %s\n", strerror(errno));
          return;
        } else {
          fprintf(wr, "300 Unknown file error.\n");
          DEBUG(warnings)("Unknown file error.\n");
        }
        continue;
      }
      //fprintf(stderr, "Command: %s", command);
      char *arg = strchr(command, ' ');
      if (!arg)
        arg = strchr(command, '\n');
      if (!arg) {
        fprintf(wr, "300 Invalid command: %s\n", command);
        continue;
      }

      *arg++ = 0;
      DEBUG(commands)("Command: %s. Arg: %s", command, arg);

#ifdef MEMCHECK
      struct mallinfo mi1 = mallinfo();
#endif
#if MEMCHECK > 1
      mtrace();
#endif

      if (!strcmp(command, "quit")) {
        if (!allow_maint)
          throw imgdb::usage_error("Not authorized");
        fprintf(wr, "100 Done.\n");
        fflush(wr);
        throw DO_QUITANDSAVE;

      } else if (!strcmp(command, "done")) {
        return;

      } else if (!strcmp(command, "list")) {
        int dbid;
        if (sscanf(arg, "%i\n", &dbid) != 1)
          throw imgdb::param_error("Format: list <dbid>");
        imgdb::imageId_list list = DB->getImgIdList();
        for (size_t i = 0; i < list.size(); i++)
          fprintf(wr, "100 %08" FMT_imageId "\n", list[i]);

      } else if (!strcmp(command, "count")) {
        int dbid;
        if (sscanf(arg, "%i\n", &dbid) != 1)
          throw imgdb::param_error("Format: count <dbid>");
        fprintf(wr, "101 count=%zd\n", DB->getImgCount());

      } else if (!strcmp(command, "query")) {
        char filename[1024];
        int dbid, flags, numres;
        if (sscanf(arg, "%i %i %i %1023[^\r\n]\n", &dbid, &flags, &numres, filename) != 4)
          throw imgdb::param_error("Format: query <dbid> <flags> <numres> <filename>");

        std::pair<char *, size_t> blob_info = filename[0] == ':' ? read_blob(filename + 1, rd) : std::make_pair<char *, size_t>(NULL, 0);
        imgdb::sim_vector sim = DB->queryImg(blob_info.first ? imgdb::queryArg(blob_info.first, blob_info.second, numres, flags) : imgdb::queryArg(filename, numres, flags));
        delete[] blob_info.first;
        fprintf(wr, "101 matches=%zd\n", sim.size());
        for (size_t i = 0; i < sim.size(); i++)
          fprintf(wr, "200 %08" FMT_imageId " %lf %d %d\n", sim[i].id, ScD(sim[i].score), sim[i].width, sim[i].height);

      } else if (!strcmp(command, "sim")) {
        int dbid, flags, numres;
        imgdb::imageId id;
        if (sscanf(arg, "%i %i %i %" FMT_imageId "\n", &dbid, &flags, &numres, &id) != 4)
          throw imgdb::param_error("Format: sim <dbid> <flags> <numres> <imageId>");

        imgdb::sim_vector sim = DB->queryImg(imgdb::queryArg(DB, id, numres, flags));
        fprintf(wr, "101 matches=%zd\n", sim.size());
        for (size_t i = 0; i < sim.size(); i++)
          fprintf(wr, "200 %08" FMT_imageId " %lf %d %d\n", sim[i].id, ScD(sim[i].score), sim[i].width, sim[i].height);

      } else if (!strcmp(command, "add")) {
        char fn[1024];
        imgdb::imageId id;
        int dbid;
        int width = -1, height = -1;
        if (sscanf(arg, "%d %" FMT_imageId " %d %d:%1023[^\r\n]\n", &dbid, &id, &width, &height, fn) != 5 &&
            sscanf(arg, "%d %" FMT_imageId ":%1023[^\r\n]\n", &dbid, &id, fn) != 3)
          throw imgdb::param_error("Format: add <dbid> <imgid>[ <width> <height>]:<filename>");

        // Could just catch imgdb::param_error, but this is so common here that handling it explicitly is better.
        if (!DB->hasImage(id)) {
          fprintf(wr, "100 Adding %s = %d:%08" FMT_imageId "...\n", fn, dbid, id);
          DB->addImage(id, fn);
        }

        if (width > 0 && height > 0)
          DB->setImageRes(id, width, height);

      } else if (!strcmp(command, "remove")) {
        imgdb::imageId id;
        int dbid;
        if (sscanf(arg, "%d %" FMT_imageId, &dbid, &id) != 2)
          throw imgdb::param_error("Format: remove <dbid> <imgid>");

        fprintf(wr, "100 Removing %d:%08" FMT_imageId "...\n", dbid, id);
        DB->removeImage(id);

      } else if (!strcmp(command, "set_res")) {
        imgdb::imageId id;
        int dbid, width, height;
        if (sscanf(arg, "%d %" FMT_imageId " %d %d\n", &dbid, &id, &width, &height) != 4)
          throw imgdb::param_error("Format: set_res <dbid> <imgid> <width> <height>");

        fprintf(wr, "100 Setting %d:%08" FMT_imageId " = %d:%d...\r", dbid, id, width, height);
        DB->setImageRes(id, width, height);

      } else if (!strcmp(command, "list_info")) {
        int dbid;
        if (sscanf(arg, "%i\n", &dbid) != 1)
          throw imgdb::param_error("Format: list_info <dbid>");
        imgdb::image_info_list list = DB->getImgInfoList();
        for (imgdb::image_info_list::iterator itr = list.begin(); itr != list.end(); ++itr)
          fprintf(wr, "100 %08" FMT_imageId " %d %d\n", itr->id, itr->width, itr->height);

      } else if (!strcmp(command, "rehash")) {
        if (!allow_maint)
          throw imgdb::usage_error("Not authorized");
        int dbid;
        if (sscanf(arg, "%d", &dbid) != 1)
          throw imgdb::param_error("Format: rehash <dbid>");

        fprintf(wr, "100 Rehashing %d...\n", dbid);
        DB->rehash();

      } else if (!strcmp(command, "coeff_stats")) {
        int dbid;
        if (sscanf(arg, "%d", &dbid) != 1)
          throw imgdb::param_error("Format: coeff_stats <dbid>");

        fprintf(wr, "100 Retrieving coefficient stats for %d...\n", dbid);
        imgdb::stats_t stats = DB->getCoeffStats();
        for (imgdb::stats_t::iterator itr = stats.begin(); itr != stats.end(); ++itr)
          fprintf(wr, "100 %d %zd\n", itr->first, itr->second);

      } else if (!strcmp(command, "saveas")) {
        if (!allow_maint)
          throw imgdb::usage_error("Not authorized");
        char fn[1024];
        int dbid;
        if (sscanf(arg, "%d %1023[^\r\n]\n", &dbid, fn) != 2)
          throw imgdb::param_error("Format: saveas <dbid> <file>");

        fprintf(wr, "100 Saving DB %d to %s...\n", dbid, fn);
        DB.save();

      } else if (!strcmp(command, "load")) {
        if (!allow_maint)
          throw imgdb::usage_error("Not authorized");
        char fn[1024], mode[32];
        int dbid;
        if (sscanf(arg, "%d %31[^\r\n ] %1023[^\r\n]\n", &dbid, mode, fn) != 3)
          throw imgdb::param_error("Format: load <dbid> <mode> <file>");
        if ((size_t)dbid < dbs.size() && dbs[dbid])
          throw imgdb::param_error("Format: dbid already in use.");

        fprintf(wr, "100 Loading DB %d from %s...\n", dbid, fn);
        dbs.at(dbid, true).load(fn, imgdb::dbSpace::mode_from_name(mode));

      } else if (!strcmp(command, "drop")) {
        if (!allow_maint)
          throw imgdb::usage_error("Not authorized");
        int dbid;
        if (sscanf(arg, "%d", &dbid) != 1)
          throw imgdb::param_error("Format: drop <dbid>");

        DB.clear();
        fprintf(wr, "100 Dropped DB %d.\n", dbid);

      } else if (!strcmp(command, "db_list")) {
        for (size_t i = 0; i < dbs.size(); i++)
          if (dbs[i])
            fprintf(wr, "102 %zd %s\n", i, dbs[i].filename().c_str());

      } else if (!strcmp(command, "ping")) {
        fprintf(wr, "100 Pong.\n");

      } else if (!strcmp(command, "debuglevel")) {
        if (strlen(arg))
          debug_level = strtol(arg, NULL, 16);
        fprintf(wr, "100 Debug level %x.\n", debug_level);

      } else if (command[0] == 0) {
        fprintf(wr, "100 NOP.\n");

      } else {
        throw command_error(command);
      }

      DEBUG(commands)("Command completed successfully.\n");

#if MEMCHECK > 1
      muntrace();
#endif
#ifdef MEMCHECK
      struct mallinfo mi2 = mallinfo();
      if (mi2.uordblks != mi1.uordblks) {
        FILE *f = fopen("memleak.log", "a");
        fprintf(f, "Command used %d bytes of memory: %s %s", mi2.uordblks - mi1.uordblks, command, arg);
        fclose(f);
      }
#endif

    } catch (const imgdb::simple_error &err) {
      fprintf(wr, "301 %s %s\n", err.type(), err.what());
      fflush(wr);
    }
}

void command(int numfiles, char **files) {
  dbSpaceAutoMap dbs(numfiles, imgdb::dbSpace::mode_alter, files);

  try {
    do_commands(stdin, stdout, dbs, true);

  } catch (const event_t &event) {
    if (event != DO_QUITANDSAVE)
      return;
    for (int dbid = 0; dbid < numfiles; dbid++)
      DB.save();
  }

  DEBUG(commands)("End of commands.\n");
}

DEFINE_ERROR(network_error, imgdb::base_error)

// Attach rd/wr FILE to fd and automatically close when going out of scope.
struct socket_stream {
  socket_stream() : socket(-1), rd(NULL), wr(NULL) {}
  socket_stream(int sock) : socket(-1), rd(NULL), wr(NULL) { set(sock); }
  void set(int sock) {
    close();

    socket = sock;
    rd = fdopen(sock, "r");
    wr = fdopen(sock, "w");

    if (sock == -1 || !rd || !wr) {
      close();
      throw network_error("Cannot fdopen socket.");
    }
  }
  ~socket_stream() { close(); }
  void close() {
    if (rd)
      fclose(rd);
    rd = NULL;
    if (wr)
      fclose(wr);
    wr = NULL;
    if (socket != -1)
      ::close(socket);
    socket = -1;
  }

  int socket;
  FILE *rd;
  FILE *wr;
};

bool set_socket(int fd, struct sockaddr_in &bindaddr, int force) {
  if (fd == -1)
    die("Can't create socket: %s\n", strerror(errno));

  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    die("Can't set SO_REUSEADDR: %s\n", strerror(errno));
  if (bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) ||
      listen(fd, 64)) {
    if (force)
      die("Can't bind/listen: %s\n", strerror(errno));
    DEBUG(base)("Socket in use, will replace server later.\n");
    return false;
  } else {
    DEBUG(base)("Listening on port %d.\n", ntohs(bindaddr.sin_port));
    return true;
  }
}

void rebind(int fd, struct sockaddr_in &bindaddr) {
  int retry = 0;
  DEBUG(base)("Binding to %08x:%d... ", ntohl(bindaddr.sin_addr.s_addr), ntohs(bindaddr.sin_port));
  while (bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr))) {
    if (retry++ > 60)
      die("Could not bind: %s.\n", strerror(errno));
    DEBUG_CONT(base)(DEBUG_OUT, "Can't bind yet: %s.\n", strerror(errno));
    sleep(1);
    DEBUG(base)("%s", "");
  }
  DEBUG_CONT(base)(DEBUG_OUT, "bind ok.\n");
  if (listen(fd, 64))
    die("Can't listen: %s.\n", strerror(errno));

  DEBUG(base)("Listening on port %d.\n", ntohs(bindaddr.sin_port));
}

void server(const char *hostport, int numfiles, char **files, bool listen2) {
  int port;
  char dummy;
  char host[1024];

  std::map<in_addr_t, bool> source_addr;
  struct addrinfo hints;
  struct addrinfo *ai;

  bzero(&hints, sizeof(hints));
  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  int ret = sscanf(hostport, "%1023[^:]:%i%c", host, &port, &dummy);
  if (ret != 2) {
    strcpy(host, "localhost");
    ret = 1 + sscanf(hostport, "%i%c", &port, &dummy);
  }
  if (ret != 2)
    die("Can't parse host/port `%s', got %d.\n", hostport, ret);

  int replace = 0;
  while (numfiles > 0) {
    if (!strcmp(files[0], "-r")) {
      replace = 1;
      numfiles--;
      files++;
    } else if (!strncmp(files[0], "-s", 2)) {
      struct sockaddr_in addr;
      if (int ret = getaddrinfo(files[0] + 2, NULL, &hints, &ai))
        die("Can't resolve host %s: %s\n", files[0] + 2, gai_strerror(ret));

      memcpy(&addr, ai->ai_addr, std::min<size_t>(sizeof(addr), ai->ai_addrlen));
      DEBUG(connections)("Restricting connections. Allowed from %s\n", inet_ntoa(addr.sin_addr));
      source_addr[addr.sin_addr.s_addr] = true;
      freeaddrinfo(ai);

      numfiles--;
      files++;
    } else {
      break;
    }
  }

  if (int ret = getaddrinfo(host, NULL, &hints, &ai))
    die("Can't resolve host %s: %s\n", host, gai_strerror(ret));

  struct sockaddr_in bindaddr_low;
  struct sockaddr_in bindaddr_high;
  memcpy(&bindaddr_low, ai->ai_addr, std::min<size_t>(sizeof(bindaddr_low), ai->ai_addrlen));
  memcpy(&bindaddr_high, ai->ai_addr, std::min<size_t>(sizeof(bindaddr_high), ai->ai_addrlen));
  bindaddr_low.sin_port = htons(port);
  bindaddr_high.sin_port = htons(port - listen2);
  freeaddrinfo(ai);

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    die("Can't ignore SIGPIPE: %s\n", strerror(errno));

  int fd_high = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  int fd_low = listen2 ? socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) : -1;
  int fd_max = listen2 ? std::max(fd_high, fd_low) : fd_high;
  bool success = set_socket(fd_high, bindaddr_high, !replace);
  if (listen2 && set_socket(fd_low, bindaddr_low, !replace) != success)
    die("Only one socket failed to bind, this is weird, aborting!\n");

  dbSpaceAutoMap dbs(numfiles, imgdb::dbSpace::mode_simple, files);

  if (!success) {
    int other_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (other_fd == -1)
      die("Can't create socket: %s.\n", strerror(errno));
    if (connect(other_fd, (struct sockaddr *)&bindaddr_high, sizeof(bindaddr_high))) {
      DEBUG(warnings)("Can't connect to old server: %s.\n", strerror(errno));
    } else {
      socket_stream stream(other_fd);
      DEBUG(base)("Sending quit command.\n");
      fputs("quit now\n", stream.wr);
      fflush(stream.wr);

      char buf[1024];
      while (fgets(buf, sizeof(buf), stream.rd))
        DEBUG(base)(" --> %s", buf);
    }

    if (listen2)
      rebind(fd_low, bindaddr_low);
    rebind(fd_high, bindaddr_high);
  }

  fd_set read_fds;
  FD_ZERO(&read_fds);

  while (1) {
    FD_SET(fd_high, &read_fds);
    if (listen2)
      FD_SET(fd_low, &read_fds);

    int nfds = select(fd_max + 1, &read_fds, NULL, NULL, NULL);
    if (nfds < 1)
      die("select() failed: %s\n", strerror(errno));

    struct sockaddr_in client;
    socklen_t len = sizeof(client);

    bool is_high = FD_ISSET(fd_high, &read_fds);

    int fd = accept(is_high ? fd_high : fd_low, (struct sockaddr *)&client, &len);
    if (fd == -1) {
      DEBUG(errors)("accept() failed: %s\n", strerror(errno));
      continue;
    }

    if (!source_addr.empty() && source_addr.find(client.sin_addr.s_addr) == source_addr.end()) {
      DEBUG(connections)("REFUSED connection from %s:%d\n", inet_ntoa(client.sin_addr), client.sin_port);
      close(fd);
      continue;
    }

    DEBUG(connections)("Accepted %s connection from %s:%d\n", is_high ? "high priority" : "normal", inet_ntoa(client.sin_addr), client.sin_port);

    struct timeval tv = {5, 0}; // 5 seconds
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv))) {
      DEBUG(errors)("Can't set SO_RCVTIMEO/SO_SNDTIMEO: %s\n", strerror(errno));
    }

    socket_stream stream;

    try {
      stream.set(fd);
      do_commands(stream.rd, stream.wr, dbs, is_high);

    } catch (const event_t &event) {
      if (event == DO_QUITANDSAVE)
        return;

    } catch (const network_error &err) {
      DEBUG(connections)("Connection %s:%d network error: %s.\n",
       inet_ntoa(client.sin_addr), client.sin_port, err.what());

      // Unhandled imgdb::base_error means it was fatal or completely unknown.
    } catch (const imgdb::base_error &err) {
      fprintf(stream.wr, "302 %s %s\n", err.type(), err.what());
      fprintf(stderr, "Caught base_error %s: %s\n", err.type(), err.what());
      throw;

    } catch (const std::exception &err) {
      fprintf(stream.wr, "300 Caught unhandled exception!\n");
      fprintf(stderr, "Caught unhandled exception: %s\n", err.what());
      throw;
    }

    DEBUG(connections)("Connection %s:%d closing.\n", inet_ntoa(client.sin_addr), client.sin_port);
  }
}

void http_server(const std::string host, const int port, const std::string database_filename) {
  httplib::Server server;
  dbSpaceAuto memory_db(database_filename.c_str(), imgdb::dbSpace::mode_simple);
  dbSpaceAuto file_db(database_filename.c_str(), imgdb::dbSpace::mode_alter);

  server.Post("/images/(\\d+)", [&](const auto &request, auto &response) {
    if (!request.has_file("file"))
      throw imgdb::param_error("`POST /images/:id` requires a `file` param");

    const imgdb::imageId post_id = std::stoi(request.matches[1]);
    if (memory_db->hasImage(post_id))
      throw imgdb::duplicate_id("Image already in database.");

    const auto &file = request.get_file_value("file");
    imgdb::ImgData signature;
    memory_db->imgDataFromBlob(file.content.c_str(), file.content.size() - 1, post_id, &signature);
    memory_db->addImageData(&signature);

    file_db->addImageData(&signature);
    file_db.save();

    json data = {
      { "id", signature.id },
      { "width", signature.width },
      { "height", signature.height },
    };

    response.set_content(data.dump(4), "application/json");
  });

  server.Delete("/images/(\\d+)", [&](const auto &request, auto &response) {
    const imgdb::imageId post_id = std::stoi(request.matches[1]);

    if (memory_db->hasImage(post_id)) {
      memory_db->removeImage(post_id);
    }

    if (file_db->hasImage(post_id)) {
      file_db->removeImage(post_id);
      file_db.save();
    }

    json data = {
      { "id", post_id },
    };

    response.set_content(data.dump(4), "application/json");
  });

  server.Post("/query", [&](const auto &request, auto &response) {
    int limit = 10;
    const int flags = 0;
    json data;

    if (!request.has_file("file"))
      throw imgdb::param_error("`POST /query` requires a `file` param");

    if (request.has_param("limit"))
      limit = stoi(request.get_param_value("limit"));

    const auto &file = request.get_file_value("file");
    const imgdb::queryArg query(file.content.c_str(), file.content.size() - 1, limit, flags);
    const auto matches = memory_db->queryImg(query);

    for (const auto &match : matches) {
      data += {
          {"id", match.id},
          {"score", ScD(match.score)},
          {"width", match.width},
          {"height", match.height},
      };
    }

    response.set_content(data.dump(4), "application/json");
  });

  server.Get("/status", [&](const auto &request, auto &response) {
    const int count = memory_db->getImgCount();
    json data = {{"images", count}};
    response.set_content(data.dump(4), "application/json");
  });

  server.set_logger([](const auto &req, const auto &res) {
    DEBUG(summary)("%s \"%s %s %s\" %d %zd\n", req.remote_addr.c_str(), req.method.c_str(), req.path.c_str(), req.version.c_str(), res.status, res.body.size());
  });

  server.set_exception_handler([](const auto& req, auto& res, std::exception &e) {
    json data = { { "message", e.what() } };

    res.set_content(data.dump(4), "application/json");
    res.status = 500;
  });

  DEBUG(summary)("Listening on %s:%i.\n", host.c_str(), port);
  server.listen(host.c_str(), port);
}

void help() {
  printf("Usage: iqdb add|list|help args...\n"
         "\tadd dbfile - Read images to add in the form ID:filename from stdin.\n"
         "\tlist dbfile - List all images in database.\n"
         "\tquery dbfile imagefile [numres] - Find similar images.\n"
         "\tsim dbfile id [numres] - Find images similar to given ID.\n"
         "\tdiff dbfile id1 id2 - Compute difference between image IDs.\n"
         "\tlisten [host:]port dbfile... - Listen on given host/port.\n"
         "\thelp - Show this help.\n");
  exit(0);
}
