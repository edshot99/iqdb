/***************************************************************************\
    imgdb.h - iqdb library API

    Copyright (C) 2008 piespy@gmail.com

    Originally based on imgSeek code, these portions
    Copyright (C) 2003 Ricardo Niederberger Cabral.

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

#ifndef IMGDBASE_H
#define IMGDBASE_H

#include <cstring>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

// STL includes
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// Haar transform defines
#include <iqdb/haar.h>
#include <iqdb/haar_signature.h>
#include <iqdb/resizer.h>

namespace imgdb {

// Global typedefs and consts.
typedef uint64_t imageId;
typedef uint64_t count_t;
typedef uint64_t offset_t;
typedef int64_t res_t;

#define __STDC_FORMAT_MACROS
#include <cinttypes>
#undef __STDC_FORMAT_MACROS

#define FMT_imageId PRIx64
#define FMT_count_t PRIu64
#define FMT_offset_t PRIu64
#define FMT_res_t PRId64

// Exceptions.
class base_error : public std::exception {
public:
  ~base_error() throw() {
    if (m_str) {
      delete m_str;
      m_str = NULL;
    }
  }
  const char *what() const throw() { return m_str ? m_str->c_str() : m_what; }
  const char *type() const throw() { return m_type; }

protected:
  base_error(const char *what, const char *type) throw() : m_what(what), m_type(type), m_str(NULL) {}
  explicit base_error(const std::string &what, const char *type) throw()
      : m_what(NULL), m_type(type), m_str(new std::string(what)) {}

  const char *m_what;
  const char *m_type;
  std::string *m_str;
};

#define DEFINE_ERROR(derived, base)                                                           \
  class derived : public base {                                                               \
  public:                                                                                     \
    derived(const char *what) throw() : base(what, #derived) {}                               \
    explicit derived(const std::string &what) throw() : base(what, #derived) {}               \
                                                                                              \
  protected:                                                                                  \
    derived(const char *what, const char *type) throw() : base(what, type) {}                 \
    explicit derived(const std::string &what, const char *type) throw() : base(what, type) {} \
  };

// Fatal, cannot recover, should discontinue using the dbSpace throwing it.
DEFINE_ERROR(fatal_error, base_error)

DEFINE_ERROR(io_error, fatal_error)       // Non-recoverable IO error while read/writing database or cache.
DEFINE_ERROR(data_error, fatal_error)     // Database has internally inconsistent data.
DEFINE_ERROR(memory_error, fatal_error)   // Database could not allocate memory.
DEFINE_ERROR(internal_error, fatal_error) // The library code has a bug.

// Non-fatal, may retry the call after correcting the problem, and continue using the library.
DEFINE_ERROR(simple_error, base_error)

DEFINE_ERROR(usage_error, simple_error) // Function call not available in current mode.
DEFINE_ERROR(param_error, simple_error) // An argument was invalid, e.g. non-existent image ID.
DEFINE_ERROR(image_error, simple_error) // Could not successfully extract image data from the given file.

// specific param_error exceptions
DEFINE_ERROR(duplicate_id, param_error) // Image ID already in DB.
DEFINE_ERROR(invalid_id, param_error)   // Image ID not found in DB.

typedef float Score;
typedef float DScore;

typedef struct {
  Score v[3];
} lumin_native;

struct sim_value {
  imageId id;
  Score score;
  sim_value(imageId id, Score score) : id(id), score(score) {};
  bool operator<(const sim_value &other) const { return score < other.score; }
};

struct image_info {
  image_info() {}
  image_info(imageId i, const lumin_native &a) : id(i), avgl(a) {}
  imageId id;
  lumin_native avgl;

  static void avglf2i(const double avglf[3], lumin_native &avgl) {
    for (int c = 0; c < 3; c++) {
      avgl.v[c] = avglf[c];
    }
  };
};

typedef std::vector<sim_value> sim_vector;
typedef Idx sig_t[NUM_COEFS];

class dbSpace {
public:
  static const int mode_simple = 0x02;   // Fast queries, less memory, cannot save, no image ID queries.
  static const int mode_alter = 0x04;    // Fast add/remove/info on existing DB file, no queries.

  virtual ~dbSpace();

  // Image queries.
  virtual sim_vector queryFromSignature(const HaarSignature& img, size_t numres = 10) = 0;
  virtual sim_vector queryFromBlob(const std::string blob, int numres = 10);

  // Stats.
  virtual size_t getImgCount() = 0;
  virtual bool hasImage(imageId id) = 0;

  // DB maintenance.
  virtual void addImageData(imageId id, const HaarSignature& signature) = 0;
  virtual void removeImage(imageId id) = 0;
  virtual void loadDatabase(std::string filename) = 0;

protected:
  dbSpace();

private:
  void operator=(const dbSpace &);
};

} // namespace imgdb

#endif
