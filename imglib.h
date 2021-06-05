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

#include <list>

#include <fstream>
#include <iostream>

#include "delta_queue.h"
#include "haar.h"
#include "imgdb.h"
#include "resizer.h"

namespace imgdb {

class dbSpaceImpl;

typedef unsigned int uint;

// Weights for the Haar coefficients.
// Straight from the referenced paper:
const float weightsf[2][6][3] = {
    // For scanned picture (sketch=0):
    //    Y      I      Q       idx total occurs
    {{5.00f, 19.21f, 34.37f}, // 0   58.58      1 (`DC' component)
     {0.83f, 1.26f, 0.36f},   // 1    2.45      3
     {1.01f, 0.44f, 0.45f},   // 2    1.90      5
     {0.52f, 0.53f, 0.14f},   // 3    1.19      7
     {0.47f, 0.28f, 0.18f},   // 4    0.93      9
     {0.30f, 0.14f, 0.27f}},  // 5    0.71      16384-25=16359

    // For handdrawn/painted sketch (sketch=1):
    //    Y      I      Q
    {{4.04f, 15.14f, 22.62f},
     {0.78f, 0.92f, 0.40f},
     {0.46f, 0.53f, 0.63f},
     {0.42f, 0.26f, 0.25f},
     {0.41f, 0.14f, 0.15f},
     {0.32f, 0.07f, 0.38f}}};

union image_id_index {
  imageId id;
  size_t index;
  image_id_index(imageId i) : id(i) {}
  image_id_index(size_t ind, bool) : index(ind) {}
  image_id_index() {}
  bool operator==(const image_id_index &other) const { return id == other.id; }
  bool operator!=(const image_id_index &other) const { return id != other.id; }

  operator size_t &() { return index; }
};

typedef std::vector<image_id_index> IdIndex_list;

struct map_iterator : public delta_iterator {
  typedef delta_iterator base_type;

  map_iterator(const size_t *idx) : base_type((delta_value *)idx) {}
  map_iterator(const base_type &itr) : base_type(itr) {}
  map_iterator() {}

  size_t get_index() const { return **this; }
};

struct mapped_file {
  mapped_file() : m_base(NULL) {}
  mapped_file(void *base, size_t len) : m_base(base), m_length(len) {}

  void unmap();

  void *m_base;
  size_t m_length;
};

struct imageIdIndex_map : public mapped_file {
  imageIdIndex_map() {}
  imageIdIndex_map(void *base, map_iterator img, map_iterator end, size_t l)
      : mapped_file(base, l), m_img(img), m_end(end) {}
  map_iterator begin() { return m_img; }
  map_iterator end() { return m_end; }

  map_iterator m_img;
  map_iterator m_end;
};

// Automatically unmaps the imageIdIndex_map when it goes out of scope.
struct AutoImageIdIndex_map : public imageIdIndex_map {
  typedef imageIdIndex_map base_type;
  AutoImageIdIndex_map(const imageIdIndex_map &map) : base_type(map) {}
  ~AutoImageIdIndex_map() { base_type::unmap(); }
};

class imageIdIndex_list {
public:
  static const size_t threshold = 0;
  class container : public delta_queue {
  public:
    struct const_iterator : public delta_iterator {
      const_iterator(const delta_iterator &itr) : delta_iterator(itr) {}
      size_t get_index() const { return **this; }
    };
  };

  imageIdIndex_map map_all() { return imageIdIndex_map(NULL, m_base.begin(), m_base.end(), 0); };
  bool empty() { return m_tail.empty() && m_base.empty(); }
  size_t size() { return m_tail.size() + m_base.size(); }
  void reserve(size_t num) {
    if (num > m_base.size())
      m_tail.reserve(num - m_base.size());
  }
  void set_base();
  void push_back(image_id_index i) { m_tail.push_back(i.index); }
  void remove(image_id_index i); // unimplemented.

  const container &tail() { return m_tail; }

  static int fd() { return -1; }

protected:
  container m_tail;
  container m_base;
};

/* in memory signature structure */
class SigStruct : public image_info {
public:
  size_t index; /* index into score array for queries */

  size_t cacheOfs;

  SigStruct(size_t ofs) : cacheOfs(ofs){};

  SigStruct(){};

  ~SigStruct() {
  }

  void init(const ImgData *nsig) {
    id = nsig->id;
    avglf2i(nsig->avglf, avgl);
  }
};

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
  SigStruct *sig() const { throw usage_error("Not valid in read-only mode."); }
  size_t index() const; // implemented below
  const lumin_native &avgl() const { return (*this)->avgl; }
  size_t cOfs() const { return index() * sizeof(ImgData); }

  dbSpaceImpl &m_db;
};

// Simplify reading/writing stream data.
#define READER_WRAPPERS                                      \
                                                             \
  template <typename T>                                      \
  T read() {                                                 \
    T dummy;                                                 \
    base_type::read((char *)&dummy, sizeof(T));              \
    return dummy;                                            \
  }                                                          \
                                                             \
  template <typename T>                                      \
  void read(T *t) { base_type::read((char *)t, sizeof(T)); } \
                                                             \
  template <typename T>                                      \
  void read(T *t, size_t n) { base_type::read((char *)t, sizeof(T) * n); }
#define WRITER_WRAPPERS                                               \
  template <typename T>                                               \
  void write(const T &t) { base_type::write((char *)&t, sizeof(T)); } \
                                                                      \
  template <typename T>                                               \
  void write(const T *t, size_t n) { base_type::write((char *)t, sizeof(T) * n); }

class db_ifstream : public std::ifstream {
public:
  typedef std::ifstream base_type;
  db_ifstream(const char *fname) : base_type(fname, std::ios::binary) {}
  READER_WRAPPERS
};

class db_ofstream : public std::ofstream {
public:
  typedef std::ofstream base_type;
  db_ofstream(const char *fname) : base_type(fname, std::ios::binary | std::ios::trunc){};
  WRITER_WRAPPERS
};

class db_fstream : public std::fstream {
public:
  typedef std::fstream base_type;

  db_fstream(const char *fname) {
    open(fname, binary | in | out);
  }

  READER_WRAPPERS
  WRITER_WRAPPERS
};

// DB space implementations.

// Common function used by all implementations.
class dbSpaceCommon : public dbSpace {
public:
  static bool is_grayscale(const lumin_native &avgl);

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

    void add(const ImgData &img, count_t index);
    void remove(const ImgData &img);

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
  dbSpaceImpl();
  virtual ~dbSpaceImpl();

  virtual void save_file(const char *filename) override;

  // Image queries.
  virtual sim_vector queryFromSignature(const ImgData& img, size_t numres = 10) override;

  // Stats.
  virtual size_t getImgCount() override;
  virtual bool hasImage(imageId id) override;

  // DB maintenance.
  virtual void addImageData(const ImgData *img) override;
  virtual void removeImage(imageId id) override;

private:
  typedef typename sigMap::iterator map_iterator;

  friend struct imageIterator;

  std::vector<image_info> &info() { return m_info; }
  imageIterator find(imageId i);

  virtual void load(const char *filename) override;

  bool skip_image(const imageIterator &itr);

  imageIterator image_begin();
  imageIterator image_end();

  sigMap m_images;

  size_t m_nextIndex;
  std::vector<image_info> m_info;

  /* Lists of picture ids, indexed by [color-channel][sign][position], i.e.,
	   R=0/G=1/B=2, pos=0/neg=1, (i*NUM_PIXELS+j)
	 */

  struct bucket_type : public imageIdIndex_list {
    typedef imageIdIndex_list base_type;
    void add(image_id_index id, count_t index) { base_type::push_back(image_id_index(index, true)); }
    void remove(image_id_index id) { base_type::remove(id); }
  };
  typedef bucket_set<bucket_type> buckets_t;
  buckets_t imgbuckets;
};

// Directly modify DB file on disk.
class dbSpaceAlter : public dbSpaceCommon {
public:
  dbSpaceAlter(const char* filename);
  virtual ~dbSpaceAlter();

  virtual void save_file(const char *filename);

  // Image queries not supported.
  virtual sim_vector queryFromSignature(const ImgData& img, size_t numres = 10) { throw usage_error("Not supported in alter mode."); }

  // Stats. Partially unsupported.
  virtual size_t getImgCount();
  virtual bool hasImage(imageId id);

  // DB maintenance.
  virtual void addImageData(const ImgData *img);

  virtual void removeImage(imageId id);

protected:
  sigMap::iterator find(imageId i);
  ImgData get_sig(size_t ind);

  virtual void load(const char *filename);

private:
  void operator=(const dbSpaceAlter &);

  void resize_header();
  void move_deleted();

  struct bucket_type {
    void add(image_id_index id, count_t index) { size++; }
    void remove(image_id_index id) { size--; }

    count_t size;
  };

  typedef std::vector<size_t> DeletedList;

  sigMap m_images;
  db_fstream *m_f;
  offset_t m_hdrOff, m_sigOff, m_imgOff;
  typedef bucket_set<bucket_type> buckets_t;
  buckets_t m_buckets;
  DeletedList m_deleted;
  bool m_rewriteIDs;
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
