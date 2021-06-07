/*
 * IQDB test suite. Uses the Catch2 testing framework.
 *
 * Compile tests with `make test-iqdb`. Run tests with `./test-iqdb`.
 *
 * https://github.com/catchorg/Catch2
 * https://github.com/catchorg/Catch2/blob/v2.x/docs/tutorial.md#writing-tests
 * https://github.com/catchorg/Catch2/blob/v2.x/docs/test-cases-and-sections.md#bdd-style-test-cases
 */

#define CATCH_CONFIG_MAIN
#include "vendor/catch.hpp"

#include "server.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cstdio>

using httplib::Client;
using httplib::Result;
using nlohmann::json;

int debug_level;

// Return the contents of a file as a string.
std::string read_file(const std::string filename) {
  auto file = std::ifstream(filename);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Add image to database by calling `POST /images/:id` with given file. Return HTTP response.
Result add_image(const int id, const std::string filename) {
  auto url = "/images/" + std::to_string(id);

  return Client("http://localhost:58000").Post(url.c_str(), {
    {"file", read_file(filename), filename, "application/octet-stream"},
  });
}

// Query image in database by calling `POST /query` with given file. Return HTTP response.
Result query_image(const std::string filename) {
  return Client("http://localhost:58000").Post("/query", {
    {"file", read_file(filename), filename, "application/octet-stream"},
  });
}

SCENARIO("Running the CLI") {
  WHEN("The `help` command is used") {
    THEN("The help text should be printed") {
      auto status = system("./iqdb help > /dev/null");
      REQUIRE(status == 0);
    }
  }
}

SCENARIO("Running the HTTP server") {
  const auto pid = fork();
  if (pid == 0) {
    http_server("localhost", 58000, "test.db");
  } else {
    sleep(1);
  }

  WHEN("The `GET /status` endpoint is called") {
    THEN("A successful response should be returned") {
      auto response = Client("http://localhost:58000").Get("/status");

      REQUIRE(response->status == 200);
      REQUIRE(json::parse(response->body) == json({{"images", 0}}));
    }
  }

  WHEN("An image is added to the database") {
    THEN("It should be returned by a subsequent query") {
      auto response = add_image(1, "files/1.jpg");
      REQUIRE(response->status == 200);
      REQUIRE(json::parse(response->body) == json({{"id", 1}, {"width", 128}, {"height", 128}}));

      response = query_image("files/1.jpg");
      REQUIRE(response->status == 200);
      REQUIRE(json::parse(response->body) == json::array({{{"id", 1}, {"score", 99.99951553344727}, {"width", 128}, {"height", 128}}}));
    }
  }

  kill(pid, SIGKILL);
  remove("test.db");
}
