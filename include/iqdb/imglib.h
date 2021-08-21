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

#include <iqdb/haar.h>
#include <iqdb/imgdb.h>
#include <iqdb/sqlite_db.h>

namespace imgdb {

// Weights for the Haar coefficients.
// Straight from the referenced paper:
const Score weights[6][3] =
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

using bucket_t = std::vector<uint32_t>;

class bucket_set {
public:
  bucket_t& at(int col, int coef);
  void add(const HaarSignature &sig, imageId iqdb_id);
  void remove(const HaarSignature &sig, imageId iqdb_id);
  void eachBucket(const HaarSignature &sig, std::function<void(bucket_t&)> func);

private:
  static const size_t n_colors  = 3;                     // 3 color channels (YIQ)
  static const size_t n_signs   = 2;                     // 2 Haar coefficient signs (positive and negative)
  static const size_t n_indexes = NUM_PIXELS*NUM_PIXELS; // 16384 Haar matrix indexes (128*128)

  // 3 * 2 * 16384 = 98304 total buckets
  bucket_t buckets[n_colors][n_signs][n_indexes];
};

// Specific implementations.
class dbSpaceImpl : public dbSpace {
public:
  dbSpaceImpl(std::string filename = ":memory:");
  virtual ~dbSpaceImpl();

  // Image queries.
  virtual sim_vector queryFromSignature(const HaarSignature& sig, size_t numres = 10) override;

  // Stats.
  virtual size_t getImgCount() override;
  bool isDeleted(imageId id); // XXX id is the iqdb id

  // DB maintenance.
  virtual void addImage(imageId id, const HaarSignature& signature) override;
  virtual std::optional<Image> getImage(imageId post_id) override;
  virtual void removeImage(imageId id) override;
  virtual void loadDatabase(std::string filename) override;

private:
  void addImageInMemory(imageId iqdb_id, imageId post_id, const HaarSignature& signature);

  std::vector<image_info> m_info;
  std::unique_ptr<SqliteDB> sqlite_db_;
  bucket_set imgbuckets;
};

} // namespace imgdb

#endif
