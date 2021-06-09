#ifndef IQDB_SQLITE_DB_H
#define IQDB_SQLITE_DB_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sqlite_orm/sqlite_orm.h>
#include <iqdb/haar_signature.h>

namespace imgdb {

// A model representing an image signature stored in the SQLite database.
struct Image {
  int64_t id;            // The internal IQDB ID.
  int64_t post_id;       // The external Danbooru post ID.
  double avglf1;         // The `double avglf[3]` array.
  double avglf2;
  double avglf3;
  std::vector<char> sig; // The `int16_t sig[3][40]` array, stored as a binary blob.
};

// Initialize the database, creating the table if it doesn't exist.
static auto initStorage(const std::string& path = ":memory:") {
  using namespace sqlite_orm;

  auto storage = make_storage(path,
    make_table("images",
      make_column("id",       &Image::id, primary_key()),
      make_column("post_id",  &Image::post_id, unique()),
      make_column("avglf1",   &Image::avglf1),
      make_column("avglf2",   &Image::avglf2),
      make_column("avglf3",   &Image::avglf3),
      make_column("sig",      &Image::sig)
    )
  );

  storage.sync_schema();
  return storage;
}

// An SQLite database containing a table of image hashes.
class SqliteDB {
public:
  using Storage = decltype(initStorage());

  // Open database at path. Default to a temporary memory-only database.
  SqliteDB(const std::string& path = ":memory:") : storage_(initStorage(path)) {};

  // Add the image to the database. Replace the image if it already exists.
  void addImage(int64_t post_id, HaarSignature signature);

  // Remove the image from the database.
  void removeImage(int64_t post_id);

  // Call a function for each image in the database.
  void eachImage(std::function<void (const Image&, const HaarSignature&)>);

  // Convert a database from the old IQDB format to the new SQLite format.
  static void convertDatabase(std::string input_filename, std::string output_filename);

private:
  // The SQLite database.
  Storage storage_;
};

}

#endif
