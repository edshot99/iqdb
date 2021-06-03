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

#include <cstddef>
#include <string>

#include "auto_clean.h"
#define DEBUG_IQDB
#include "debug.h"
#include "imgdb.h"

#include "vendor/httplib.h"
#include "vendor/json.hpp"

using nlohmann::json;
using httplib::Server;

extern int debug_level;

class dbSpaceAuto : public AutoCleanPtr<imgdb::dbSpace> {
public:
  dbSpaceAuto(){};
  dbSpaceAuto(const char *filename, int mode) : AutoCleanPtr<imgdb::dbSpace>(loaddb(filename, mode)), m_filename(filename){};

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

#ifdef INTMATH
#define ScD(x) ((double)(x) / imgdb::ScoreMax)
#else
#define ScD(x) (x)
#endif

static Server server;

void install_signal_handlers() {
  struct sigaction action = {};
  sigfillset(&action.sa_mask);
  action.sa_flags = SA_RESTART;

  action.sa_handler = [](int) {
    if (server.is_running()) {
      server.stop();
    }
  };

  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
}

void http_server(const std::string host, const int port, const std::string database_filename) {
  DEBUG(summary)("Starting server...\n");

  dbSpaceAuto memory_db(database_filename.c_str(), imgdb::dbSpace::mode_simple);
  dbSpaceAuto file_db(database_filename.c_str(), imgdb::dbSpace::mode_alter);

  install_signal_handlers();

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
    json data;

    if (!request.has_file("file"))
      throw imgdb::param_error("`POST /query` requires a `file` param");

    if (request.has_param("limit"))
      limit = stoi(request.get_param_value("limit"));

    const auto &file = request.get_file_value("file");
    const imgdb::queryArg query(file.content.c_str(), file.content.size() - 1, limit);
    const auto matches = memory_db->queryImg(query);

    for (const auto &match : matches) {
      data += {
          {"id", match.id},
          {"score", ScD(match.score)},
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
  DEBUG(summary)("Stopping server...\n");
}

void help() {
  printf(
    "Usage: iqdb COMMAND [ARGS...]\n"
    "  iqdb http [host] [port] [dbfile]  Run HTTP server on given host/port.\n"
    "  iqdb help                         Show this help.\n"
  );

  exit(0);
}
