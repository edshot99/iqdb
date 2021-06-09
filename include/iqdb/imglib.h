/***************************************************************************\
    imglib.h - iqdb library internal definitions

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

/**************************************************************************\
Implementation notes:

The abstract dbSpace class is implemented as two different dbSpaceImpl
classes depending on a bool template parameter indicating whether the DB is
read-only or not. The "false" version has full functionality but is not
optimized for querying. The "true" version is read-only but has much faster
queries. The read-only version can optionally discard image data not needed
for querying from external image files to further reduce memory usage. This
is called "simple mode".

There is an additional "alter" version that modifies the DB file directly
instead of first loading it into memory, like the other modes do.

The advantage of this design is maximum code re-use for the two DB usage
patterns: maintenance and querying. Both implementation classes use
different variables, and specifically different iterators to iterate over
all images. The implementation details of the iterators are of course
different but the majority of the actual code is the same for both types.
\**************************************************************************/

#ifndef IMGDBLIB_H
#define IMGDBLIB_H

#include <algorithm>
#include <list>

#include <fstream>
#include <iostream>

#include <iqdb/haar.h>
#include <iqdb/imgdb.h>
#include <iqdb/resizer.h>
#include <iqdb/sqlite_db.h>

namespace imgdb {

class dbSpaceImpl;

// Weights for the Haar coefficients.
// Straight from the referenced paper:
const float weights[6][3] =
    // For scanned picture (sketch=0):
    //    Y      I      Q       idx total occurs
    {{5.00f, 19.21f, 34.37f}, // 0   58.58      1 (`DC' component)
     {0.83f, 1.26f, 0.36f},   // 1    2.45      3
     {1.01f, 0.44f, 0.45f},   // 2    1.90      5
     {0.52f, 0.53f, 0.14f},   // 3    1.19      7
     {0.47f, 0.28f, 0.18f},   // 4    0.93      9
     {0.30f, 0.14f, 0.27f}};  // 5    0.71      16384-25=16359

// A 128x128 weight mask matrix, where M[x][y] = min(max(x, y), 5). Used in
// score calculation.
//
// 0 1 2 3 4 5 5 ...
// 1 1 2 3 4 5 5 ...
// 2 2 2 3 4 5 5 ...
// 3 3 3 3 4 5 5 ...
// 4 4 4 4 4 5 5 ...
// 5 5 5 5 5 5 5 ...
// 5 5 5 5 5 5 5 ...
// . . . . . . .
// . . . . . . .
// . . . . . . .
template <int N>
struct ImgBin {
  int bin[N * N];

  constexpr ImgBin() : bin() {
    for (int i = 0; i < N; i++)
      for (int j = 0; j < N; j++)
        bin[i * N + j] = std::min(std::max(i, j), 5);
  }
};

constexpr static auto imgBin = ImgBin<NUM_PIXELS>();

class sigMap : public std::unordered_map<imageId, size_t> {
public:
  void add_index(imageId id, size_t index) { (*this)[id] = index; }
};

// In simple mode, we have only the image_info data available, so iterate over that.
// In read-only mode, we additionally have the index into the image_info array in a sigMap.
// Using functions that rely on this in simple mode will throw a usage_error.
struct imageIterator : public std::vector<image_info>::iterator {
  typedef std::vector<image_info>::iterator base_type;
  imageIterator(const base_type &itr, dbSpaceImpl &db) : base_type(itr), m_db(db) {}
  imageIterator(const sigMap::iterator &itr, dbSpaceImpl &db); // implemented below

  imageId id() const { return (*this)->id; }
  size_t index() const; // implemented below
  const lumin_native &avgl() const { return (*this)->avgl; }

  dbSpaceImpl &m_db;
};

// DB space implementations.

// Common function used by all implementations.
class dbSpaceCommon : public dbSpace {
public:
  static const int mode_mask_simple = 0x02;
  static const int mode_mask_alter = 0x04;

protected:
  template <typename B>
  class bucket_set {
  public:
    static const size_t count_0 = 3;     // Colors
    static const size_t count_1 = 2;     // Coefficient signs.
    static const size_t count_2 = 16384; // Coefficient magnitudes.

    // Do some sanity checking to ensure the array is layed out how we think it is layed out.
    // (Otherwise the iterators will be broken.)
    bucket_set() {
      if ((size_t)(end() - begin()) != count() || (size_t)((char *)end() - (char *)begin()) != size() || size() != sizeof(*this))
        throw internal_error("bucket_set array packed badly.");
    }

    typedef B *iterator;
    typedef B colbucket[count_1][count_2];

    colbucket &operator[](size_t ind) { return buckets[ind]; }
    B &at(int col, int coef, int *idxret = NULL);

    void add(const HaarSignature &sig, count_t index);
    void remove(const HaarSignature &sig, imageId post_id);

    iterator begin() { return buckets[0][0]; }
    iterator end() { return buckets[count_0][0]; }

    static count_t count() { return count_0 * count_1 * count_2; }
    static count_t size() { return count() * sizeof(B); }

  private:
    colbucket buckets[count_0];
  };

private:
  void operator=(const dbSpaceCommon &);
};

// Specific implementations.
class dbSpaceImpl : public dbSpaceCommon {
public:
  dbSpaceImpl(std::string filename = ":memory:");
  virtual ~dbSpaceImpl();

  // Image queries.
  virtual sim_vector queryFromSignature(const HaarSignature& sig, size_t numres = 10) override;

  // Stats.
  virtual size_t getImgCount() override;
  virtual bool hasImage(imageId id) override;
  bool isDeleted(imageId id); // XXX id is the iqdb id

  // DB maintenance.
  virtual void addImageData(imageId id, const HaarSignature& signature) override;
  virtual void removeImage(imageId id) override;
  virtual void loadDatabase(std::string filename) override;

private:
  friend struct imageIterator;

  std::vector<image_info> &info() { return m_info; }
  imageIterator find(imageId i);

  sigMap m_images;

  size_t m_nextIndex;
  std::vector<image_info> m_info;

  /* Lists of picture ids, indexed by [color-channel][sign][position], i.e.,
	   R=0/G=1/B=2, pos=0/neg=1, (i*NUM_PIXELS+j)
	 */

  // XXX We use a uint32_t here to reduce memory consumption.
  struct bucket_type : public std::vector<uint32_t> {
    void add(count_t index) { push_back(index); }
    void remove(imageId id) { throw usage_error("remove not implemented"); }
  };

  typedef bucket_set<bucket_type> buckets_t;
  buckets_t imgbuckets;
  std::unique_ptr<SqliteDB> sqlite_db_;
};

// Serialization constants
static const unsigned int SRZ_V0_9_0 = 9;

// Variable size and endianness check
static const uint32_t SRZ_V_SZ = (sizeof(res_t)) |
                                 (sizeof(count_t) << 5) |
                                 (sizeof(offset_t) << 10) |
                                 (sizeof(imageId) << 15) |
                                 (3 << 20); // never matches any of the above for endian check

static const uint32_t SRZ_V_CODE = (SRZ_V0_9_0) | (SRZ_V_SZ << 8);

// Delayed implementations.
inline imageIterator::imageIterator(const sigMap::iterator &itr, dbSpaceImpl &db)
    : base_type(db.info().begin() + itr->second), m_db(db) {}
inline size_t imageIterator::index() const { return *this - m_db.info().begin(); }

} // namespace imgdb

#endif
