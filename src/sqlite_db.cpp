#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include <iqdb/debug.h>
#include <iqdb/imglib.h>
#include <iqdb/sqlite_db.h>
#include <iqdb/types.h>

namespace imgdb {

using namespace sqlite_orm;

// An image in the old (non-SQLite) database format.
struct ImgData {
  uint64_t post_id;   // Danbooru post_id
  int16_t sig[3][40]; // YIQ positions with largest magnitude
  double avglf[3];    // YIQ for position [0,0]
  int64_t width;      // in pixels (unused)
  int64_t height;     // in pixels (unused)
};

// Read a value of type T from the stream.
template <typename T>
static T read(std::ifstream& stream) {
  T dummy;
  stream.read((char *)&dummy, sizeof(T));
  return dummy;
}

HaarSignature Image::haar() const {
  lumin_t avglf = { avglf1, avglf2, avglf3 };
  return HaarSignature(avglf, *(signature_t*)sig.data());
}

void SqliteDB::eachImage(std::function<void (const Image&)> func) {
  for (auto& image : storage_.iterate<Image>()) {
    func(image);
  }
}

std::optional<Image> SqliteDB::getImage(postId post_id) {
  auto results = storage_.get_all<Image>(where(c(&Image::post_id) == post_id));

  if (results.size() == 1) {
    return results[0];
  } else {
    DEBUG("Couldn't find post #{} in sqlite database.\n", post_id);
    return std::nullopt;
  }
}

int SqliteDB::addImage(postId post_id, HaarSignature signature) {
  int id = -1;
  auto sig_ptr = (const char*)signature.sig;
  std::vector<char> sig_blob(sig_ptr, sig_ptr + sizeof(signature.sig));
  Image image {
    0, post_id, signature.avglf[0], signature.avglf[1], signature.avglf[2], sig_blob
  };

  storage_.transaction([&] {
    removeImage(post_id);
    id = storage_.insert(image);
    return true;
  });

  return id;
}

void SqliteDB::removeImage(postId post_id) {
  storage_.remove_all<Image>(where(c(&Image::post_id) == post_id));
}

void SqliteDB::convertDatabase(std::string input_filename, std::string output_filename) {
  INFO("Converting db from {} to {}...\n", input_filename, output_filename);

  std::ifstream f(input_filename, std::ios::in | std::ios::binary);
  f.exceptions(std::ifstream::failbit | std::ifstream::badbit | std::ifstream::eofbit);

  if (!f.is_open()) {
    WARN("Unable to open file {} for conversion: {}.\n", input_filename, strerror(errno));
    return;
  }

  Storage storage = initStorage(output_filename);
  storage.pragma.synchronous(false);
  storage.pragma.journal_mode(journal_mode::OFF);

  uint32_t v_code = read<uint32_t>(f);
  uint32_t intsizes = v_code >> 8;
  unsigned int version = v_code & 0xff;

  if (intsizes != SRZ_V_SZ) {
    throw fatal_error("Cannot load database with wrong endianness or data sizes");
  } else if (version != SRZ_V0_9_0) {
    throw fatal_error("Database is from an unsupported version (not 0.9.0)");
  }

  uint64_t numImg = read<uint64_t>(f);
  uint64_t sigOffset = read<uint64_t>(f);
  f.seekg(sigOffset);

  INFO("{} has {} images at {:x}.\n", input_filename, numImg, sigOffset);

  storage.transaction([&] {
    for (uint64_t k = 0; k < numImg; k++) {
      ImgData img = read<ImgData>(f);

      std::sort(&img.sig[0][0], &img.sig[0][NUM_COEFS]);
      std::sort(&img.sig[1][0], &img.sig[1][NUM_COEFS]);
      std::sort(&img.sig[2][0], &img.sig[2][NUM_COEFS]);

      std::vector<char> sig_blob((char*)img.sig, (char*)img.sig + sizeof(img.sig));

      try {
        storage.insert(Image {
          0, (uint32_t)img.post_id, img.avglf[0], img.avglf[1], img.avglf[2], sig_blob
        });
      } catch (const std::system_error& err) { // thrown when post_id uniqueness constraint fails
        INFO("Skipping duplicate post #{}\n", img.post_id);
      }

      if (k % 10000 == 0) {
        INFO("Image {} (post #{})...\n", k, img.post_id);
      }
    }

    return true;
  });

  INFO("Converted database from {}!\n", input_filename);
  f.close();
}

}
