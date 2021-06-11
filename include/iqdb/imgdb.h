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

#include <memory>
#include <string>
#include <vector>

#include <iqdb/haar.h>
#include <iqdb/haar_signature.h>
#include <iqdb/resizer.h>

namespace imgdb {

// Global typedefs and consts.
typedef uint64_t imageId;

// Exceptions.
class base_error : public std::exception {
public:
  base_error(std::string what) noexcept : what_(what) {}
  const char* what() const noexcept { return what_.c_str(); }

  const std::string what_;
};

#define DEFINE_ERROR(derived, base)                            \
  class derived : public base {                                \
  public:                                                      \
    derived(std::string what) noexcept : base(what) {}         \
  };

// Fatal error, cannot recover.
DEFINE_ERROR(fatal_error, base_error)

// Non-fatal, may retry the call after correcting the problem, and continue using the library.
DEFINE_ERROR(simple_error, base_error)
DEFINE_ERROR(param_error, simple_error) // An argument was invalid, e.g. non-existent image ID.
DEFINE_ERROR(image_error, simple_error) // Could not successfully extract image data from the given file.

typedef float Score;

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
};

typedef std::vector<sim_value> sim_vector;
typedef Idx sig_t[NUM_COEFS];

class dbSpace {
public:
  virtual ~dbSpace();

  // Image queries.
  virtual sim_vector queryFromSignature(const HaarSignature& img, size_t numres = 10) = 0;
  virtual sim_vector queryFromBlob(const std::string blob, int numres = 10);

  // Stats.
  virtual size_t getImgCount() = 0;

  // DB maintenance.
  virtual void addImage(imageId id, const HaarSignature& signature) = 0;
  virtual void removeImage(imageId id) = 0;
  virtual void loadDatabase(std::string filename) = 0;

protected:
  dbSpace();

private:
  void operator=(const dbSpace &);
};

} // namespace imgdb

#endif
