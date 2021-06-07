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

#include <csignal>
#include <cstddef>
#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include <iqdb/debug.h>
#include <iqdb/imgdb.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

using nlohmann::json;
using httplib::Server;
using imgdb::dbSpace;

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
  INFO("Starting server...\n");

  std::shared_mutex mutex_;
  auto memory_db = dbSpace::load_file(database_filename.c_str(), dbSpace::mode_simple);
  auto file_db = dbSpace::load_file(database_filename.c_str(), dbSpace::mode_alter);

  install_signal_handlers();

  server.Post("/images/(\\d+)", [&](const auto &request, auto &response) {
    std::unique_lock lock(mutex_);

    if (!request.has_file("file"))
      throw imgdb::param_error("`POST /images/:id` requires a `file` param");

    const imgdb::imageId post_id = std::stoi(request.matches[1]);
    if (memory_db->hasImage(post_id))
      throw imgdb::duplicate_id("Image already in database.");

    const auto &file = request.get_file_value("file");

    imgdb::ImgData signature(file.content, post_id);
    memory_db->addImageData(&signature);
    file_db->addImageData(&signature);
    file_db->save_file(NULL);

    json data = {
      { "id", signature.id },
    };

    response.set_content(data.dump(4), "application/json");
  });

  server.Delete("/images/(\\d+)", [&](const auto &request, auto &response) {
    std::unique_lock lock(mutex_);

    const imgdb::imageId post_id = std::stoi(request.matches[1]);

    if (memory_db->hasImage(post_id)) {
      memory_db->removeImage(post_id);
    }

    if (file_db->hasImage(post_id)) {
      file_db->removeImage(post_id);
      file_db->save_file(NULL);
    }

    json data = {
      { "id", post_id },
    };

    response.set_content(data.dump(4), "application/json");
  });

  server.Post("/query", [&](const auto &request, auto &response) {
    std::shared_lock lock(mutex_);

    int limit = 10;
    json data;

    if (!request.has_file("file"))
      throw imgdb::param_error("`POST /query` requires a `file` param");

    if (request.has_param("limit"))
      limit = stoi(request.get_param_value("limit"));

    const auto &file = request.get_file_value("file");
    const auto matches = memory_db->queryFromBlob(file.content, limit);

    for (const auto &match : matches) {
      data += {
          {"id", match.id},
          {"score", match.score},
      };
    }

    response.set_content(data.dump(4), "application/json");
  });

  server.Get("/status", [&](const auto &request, auto &response) {
    std::shared_lock lock(mutex_);

    const int count = memory_db->getImgCount();
    json data = {{"images", count}};

    response.set_content(data.dump(4), "application/json");
  });

  server.set_logger([](const auto &req, const auto &res) {
    INFO("%s \"%s %s %s\" %d %zd\n", req.remote_addr.c_str(), req.method.c_str(), req.path.c_str(), req.version.c_str(), res.status, res.body.size());
  });

  server.set_exception_handler([](const auto& req, auto& res, std::exception &e) {
    json data = { { "message", e.what() } };

    res.set_content(data.dump(4), "application/json");
    res.status = 500;
  });

  INFO("Listening on %s:%i.\n", host.c_str(), port);
  server.listen(host.c_str(), port);
  INFO("Stopping server...\n");
}

void help() {
  printf(
    "Usage: iqdb COMMAND [ARGS...]\n"
    "  iqdb http [host] [port] [dbfile]  Run HTTP server on given host/port.\n"
    "  iqdb help                         Show this help.\n"
  );

  exit(0);
}
