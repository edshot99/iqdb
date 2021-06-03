/***************************************************************************\
    imgdb.cpp - iqdb library implementation

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

/* System includes */
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

/* STL includes */
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

/* iqdb includes */
#include "debug.h"
#include "imgdb.h"
#include "imglib.h"

extern int debug_level;

namespace imgdb {

// Globals
#ifdef INTMATH
Score weights[2][6][3];

#define ScD(x) ((double)(x) / imgdb::ScoreMax)
#define DScD(x) ScD(((x) / imgdb::ScoreMax))
#define DScSc(x) ((x) >> imgdb::ScoreScale)
#else
#define weights weightsf

#define ScD(x) (x)
#define DScD(x) (x)
#define DScSc(x) (x)
#endif

/* Fixed weight mask for pixel positions (i,j).
Each entry x = i*NUM_PIXELS + j, gets value max(i,j) saturated at 5.
To be treated as a constant.
 */
unsigned char imgBin[NUM_PIXELS * NUM_PIXELS];
int imgBinInited = 0;

static size_t pageSize = 0;
static size_t pageMask = 0;
static size_t pageImgs = 0;
static size_t pageImgMask = 0;

inline void mapped_file::unmap() {
  if (!m_base)
    return;
  if (munmap(m_base, m_length))
    DEBUG(errors)("WARNING: Could not unmap %zd bytes of memory.\n", m_length);
}

void imageIdIndex_list::set_base() {
  if (!m_base.empty())
    return;

  if (m_tail.base_size() * 17 / 16 + 16 < m_tail.base_capacity()) {
    container copy;
    copy.reserve(m_tail.base_size(), true);
    for (container::iterator itr = m_tail.begin(); itr != m_tail.end(); ++itr)
      copy.push_back(*itr);

    m_base.swap(copy);
    copy = container();
    m_tail.swap(copy);
  } else {
    m_base.swap(m_tail);
  }
}

// Specializations accessing images as SigStruct* or size_t map, and imageIdIndex_map as imageId or index map.
inline imageIterator dbSpaceImpl::image_begin() { return imageIterator(m_info.begin(), *this); }
inline imageIterator dbSpaceImpl::image_end() { return imageIterator(m_info.end(), *this); }

inline imageIterator dbSpaceImpl::find(imageId i) {
  map_iterator itr = m_images.find(i);
  if (itr == m_images.end())
    throw invalid_id("Invalid image ID.");
  return imageIterator(itr, *this);
}

inline sigMap::iterator dbSpaceAlter::find(imageId i) {
  sigMap::iterator itr = m_images.find(i);
  if (itr == m_images.end())
    throw invalid_id("Invalid image ID.");
  return itr;
}

void initImgBin() {
  imgBinInited = 1;
  srand((unsigned)time(0));

  pageSize = sysconf(_SC_PAGESIZE);
  pageMask = pageSize - 1;
  pageImgs = pageSize / sizeof(imageId);
  pageImgMask = pageMask / sizeof(imageId);
  //fprintf(stderr, "page size = %zx, page mask = %zx.\n", pageSize, pageMask);

  /* setup initial fixed weights that each coefficient represents */
  int i, j;

  /*
    0 1 2 3 4 5 6 i
    0 0 1 2 3 4 5 5
    1 1 1 2 3 4 5 5
    2 2 2 2 3 4 5 5
    3 3 3 3 3 4 5 5
    4 4 4 4 4 4 5 5
    5 5 5 5 5 5 5 5
    5 5 5 5 5 5 5 5
    j
  */

  /* Every position has value 5, */
  memset(imgBin, 5, NUM_PIXELS_SQUARED);

  /* Except for the 5 by 5 upper-left quadrant: */
  for (i = 0; i < 5; i++)
    for (j = 0; j < 5; j++)
      imgBin[i * NUM_PIXELS + j] = std::max(i, j);
      // Note: imgBin[0] == 0

      /* Integer weights. */
#ifdef INTMATH
  for (i = 0; i < 2; i++)
    for (j = 0; j < 6; j++)
      for (int c = 0; c < 3; c++)
        weights[i][j][c] = lrint(weightsf[i][j][c] * ScoreMax);
#endif
}

bool dbSpaceImpl::hasImage(imageId id) {
  return m_images.find(id) != m_images.end();
}

bool dbSpaceAlter::hasImage(imageId id) {
  return m_images.find(id) != m_images.end();
}

inline ImgData dbSpaceAlter::get_sig(size_t ind) {
  ImgData sig;
  m_f->seekg(m_sigOff + ind * sizeof(ImgData));
  m_f->read(&sig);
  return sig;
}

inline bool dbSpaceCommon::is_grayscale(const lumin_native &avgl) {
  return std::abs(avgl.v[1]) + std::abs(avgl.v[2]) < MakeScore(6) / 1000;
}

bool dbSpaceCommon::isImageGrayscale(imageId id) {
  lumin_native avgl;
  getImgAvgl(id, avgl);
  return is_grayscale(avgl);
}

void dbSpaceCommon::sigFromImage(Image *image, imageId id, ImgData *sig) {
  std::vector<unsigned char> rchan(NUM_PIXELS * NUM_PIXELS);
  std::vector<unsigned char> gchan(NUM_PIXELS * NUM_PIXELS);
  std::vector<unsigned char> bchan(NUM_PIXELS * NUM_PIXELS);

  sig->id = id;
  sig->width = 0;
  sig->height = 0;

  AutoGDImage resized;
  if (image->sx != NUM_PIXELS || image->sy != NUM_PIXELS || !gdImageTrueColor(image)) {
    resized.set(gdImageCreateTrueColor(NUM_PIXELS, NUM_PIXELS));
    gdImageFilledRectangle(resized, 0, 0, NUM_PIXELS, NUM_PIXELS, gdTrueColor(255, 255, 255));
    gdImageCopyResampled(resized, image, 0, 0, 0, 0, NUM_PIXELS, NUM_PIXELS, image->sx, image->sy);
    image = resized;
  }

  for (int y = 0; y < NUM_PIXELS; y++) {
    for (int x = 0; x < NUM_PIXELS; x++) {
      // https://libgd.github.io/manuals/2.3.1/files/gd-c.html#gdImageGetPixel
      // https://libgd.github.io/manuals/2.3.1/files/gd-h.html#gdTrueColorGetRed
      int pixel = gdImageGetPixel(image, x, y);
      rchan[x + y * NUM_PIXELS] = gdTrueColorGetRed(pixel);
      gchan[x + y * NUM_PIXELS] = gdTrueColorGetGreen(pixel);
      bchan[x + y * NUM_PIXELS] = gdTrueColorGetBlue(pixel);
    }
  }

  std::vector<Unit> cdata1(NUM_PIXELS * NUM_PIXELS);
  std::vector<Unit> cdata2(NUM_PIXELS * NUM_PIXELS);
  std::vector<Unit> cdata3(NUM_PIXELS * NUM_PIXELS);
  transformChar(rchan.data(), gchan.data(), bchan.data(), cdata1.data(), cdata2.data(), cdata3.data());
  calcHaar(cdata1.data(), cdata2.data(), cdata3.data(), sig->sig1, sig->sig2, sig->sig3, sig->avglf);
}

template <typename B>
inline void dbSpaceCommon::bucket_set<B>::add(const ImgData &nsig, count_t index) {
  lumin_native avgl;
  SigStruct::avglf2i(nsig.avglf, avgl);
  for (int i = 0; i < NUM_COEFS; i++) { // populate buckets

    //imageId_array3 (imgbuckets = dbSpace[dbId]->(imgbuckets;
    if (nsig.sig1[i] > 0)
      buckets[0][0][nsig.sig1[i]].add(nsig.id, index);
    if (nsig.sig1[i] < 0)
      buckets[0][1][-nsig.sig1[i]].add(nsig.id, index);

    if (is_grayscale(avgl))
      continue; // ignore I/Q coeff's if chrominance too low

    if (nsig.sig2[i] > 0)
      buckets[1][0][nsig.sig2[i]].add(nsig.id, index);
    if (nsig.sig2[i] < 0)
      buckets[1][1][-nsig.sig2[i]].add(nsig.id, index);

    if (nsig.sig3[i] > 0)
      buckets[2][0][nsig.sig3[i]].add(nsig.id, index);
    if (nsig.sig3[i] < 0)
      buckets[2][1][-nsig.sig3[i]].add(nsig.id, index);
  }
}

template <typename B>
inline B &dbSpaceCommon::bucket_set<B>::at(int col, int coeff, int *idxret) {
  int pn, idx;

  pn = 0;
  if (coeff > 0) {
    pn = 0;
    idx = coeff;
  } else {
    pn = 1;
    idx = -coeff;
  }

  if (idxret)
    *idxret = idx;
  return buckets[col][pn][idx];
}

void dbSpaceImpl::addImageData(const ImgData *img) {
  if (hasImage(img->id)) // image already in db
    throw duplicate_id("Image already in database.");

  size_t ind = m_nextIndex++;
  if (ind > m_info.size())
    throw internal_error("Index incremented too much!");
  if (ind == m_info.size()) {
    if (ind >= m_info.capacity())
      m_info.reserve(10 + ind + ind / 40);
    m_info.resize(ind + 1);
  }
  m_info.at(ind).id = img->id;
  SigStruct::avglf2i(img->avglf, m_info[ind].avgl);
  m_images.add_index(img->id, ind);

  imgbuckets.add(*img, ind);
}

void dbSpaceAlter::addImageData(const ImgData *img) {
  if (hasImage(img->id)) // image already in db
    throw duplicate_id("Image already in database.");

  size_t ind;
  if (!m_deleted.empty()) {
    ind = m_deleted.back();
    m_deleted.pop_back();
  } else {
    ind = m_images.size();
    if (m_imgOff + ((off_t)ind + 1) * (off_t)sizeof(imageId) >= m_sigOff) {
      resize_header();
      if (m_imgOff + ((off_t)ind + 1) * (off_t)sizeof(imageId) >= m_sigOff)
        throw internal_error("resize_header failed!");
    }
  }

  if (!m_rewriteIDs) {
    m_f->seekp(m_imgOff + ind * sizeof(imageId));
    m_f->write(img->id);
  }
  m_f->seekp(m_sigOff + ind * sizeof(ImgData));
  m_f->write(*img);

  m_buckets.add(*img, ind);
  m_images[img->id] = ind;
}

void dbSpaceCommon::addImageBlob(imageId id, const void *blob, size_t length) {
  ImgData sig;
  imgDataFromBlob(blob, length, id, &sig);
  return addImageData(&sig);
}

void dbSpaceCommon::imgDataFromBlob(const void *data, size_t data_size, imageId id, ImgData *img) {
  AutoGDImage image(resize_image_data((const unsigned char *)data, data_size, NUM_PIXELS, NUM_PIXELS));
  sigFromImage(image, id, img);
}

void dbSpace::imgDataFromBlob(const void *data, size_t data_size, imageId id, ImgData *img) {
  return dbSpaceCommon::imgDataFromBlob(data, data_size, id, img);
}

void dbSpaceImpl::load(const char *filename) {
  DEBUG(imgdb)("Loading db (simple) from %s... ", filename);
  db_ifstream f(filename);

  if (!f.is_open()) {
    DEBUG(summary)("Unable to open file %s for read ops: %s.\n", filename, strerror(errno));
    return;
  }

  uint32_t v_code = f.read<uint32_t>();
  uint32_t intsizes = v_code >> 8;
  uint version = v_code & 0xff;

  if (intsizes != SRZ_V_SZ) {
    throw data_error("Cannot load database with wrong endianness or data sizes");
  } else if (version != SRZ_V0_9_0) {
    throw data_error("Database is from an unsupported version (not 0.9.0)");
  }

  count_t numImg = f.read<count_t>();
  offset_t firstOff = f.read<offset_t>();
  DEBUG_CONT(imgdb)(DEBUG_OUT, "has %" FMT_count_t " images at %llx. ", numImg, (long long)firstOff);

  // read bucket sizes and reserve space so that buckets do not
  // waste memory due to exponential growth of std::vector
  for (typename buckets_t::iterator itr = imgbuckets.begin(); itr != imgbuckets.end(); ++itr)
    itr->reserve(f.read<count_t>());
  DEBUG_CONT(imgdb)(DEBUG_OUT, "bucket sizes done at %llx... ", (long long)firstOff);

  // read IDs (for verification only).
  std::vector<imageId> ids(numImg);
  f.read(ids.data(), numImg);

  // read sigs
  f.seekg(firstOff);
  m_info.resize(numImg);
  for (sigMap::size_type k = 0; k < numImg; k++) {
    ImgData sig;
    f.read(&sig);

    size_t ind = m_nextIndex++;
    imgbuckets.add(sig, ind);

    if (ids.at(ind) != sig.id) {
      DEBUG(warnings)("WARNING: index %zd DB header ID %08llx mismatch with sig ID %08llx.\n", ind, (long long)ids[ind], (long long)sig.id);
    }

    m_info[ind].id = sig.id;
    SigStruct::avglf2i(sig.avglf, m_info[ind].avgl);

    m_images.add_index(sig.id, ind);
  }

  for (typename buckets_t::iterator itr = imgbuckets.begin(); itr != imgbuckets.end(); ++itr)
    itr->set_base();
  m_bucketsValid = true;
  DEBUG_CONT(imgdb)(DEBUG_OUT, "complete!\n");
  f.close();
}

void dbSpaceAlter::load(const char *filename) {
  DEBUG(imgdb)("Loading db (alter) from %s... ", filename);
  delete m_f;
  m_f = new db_fstream(filename);
  m_fname = filename;
  try {
    if (!m_f->is_open()) {
      // Instead of replicating code here to create the basic file structure, we'll just make a dummy DB.
      auto dummy = std::make_unique<dbSpaceImpl>();
      dummy->save_file(filename);

      m_f->open(filename);
      if (!m_f->is_open())
        throw io_error("Could not create DB structure.");
    }
    m_f->exceptions(std::fstream::badbit | std::fstream::failbit);

    uint32_t v_code = m_f->read<uint32_t>();
    uint version = v_code & 0xff;

    if ((v_code >> 8) == 0) {
      DEBUG(warnings)("Old database version.\n");
    } else if ((v_code >> 8) != SRZ_V_SZ) {
      throw data_error("Database incompatible with this system");
    }

    if (version != SRZ_V0_9_0)
      throw data_error("Only current version is supported in alter mode, upgrade first using normal mode.");

    DEBUG(imgdb)("Loading db header (cur ver)... ");
    m_hdrOff = m_f->tellg();
    count_t numImg = m_f->read<count_t>();
    m_sigOff = m_f->read<offset_t>();

    DEBUG_CONT(imgdb)(DEBUG_OUT, "has %" FMT_count_t " images. ", numImg);
    // read bucket sizes
    for (buckets_t::iterator itr = m_buckets.begin(); itr != m_buckets.end(); ++itr)
      itr->size = m_f->read<count_t>();

    // read IDs
    m_imgOff = m_f->tellg();
    for (size_t k = 0; k < numImg; k++)
      m_images[m_f->read<count_t>()] = k;

    m_rewriteIDs = false;
    DEBUG_CONT(imgdb)(DEBUG_OUT, "complete!\n");
  } catch (const base_error &e) {
    if (m_f) {
      if (m_f->is_open())
        m_f->close();
      delete m_f;
      m_fname.clear();
    }
    DEBUG_CONT(imgdb)(DEBUG_OUT, "failed!\n");
    throw;
  }
}

dbSpace *dbSpace::load_file(const char *filename, int mode) {
  dbSpace* db;

  if (mode == dbSpaceCommon::mode_mask_alter) {
    db = new dbSpaceAlter(filename);
  } else if (mode == dbSpaceCommon::mode_mask_simple) {
    db = new dbSpaceImpl();
  } else {
    throw usage_error("Unsupported database mode");
  }

  db->load(filename);
  return db;
}

void dbSpaceImpl::save_file(const char* filename) {
  /*
    Serialization order:
    [sigMap::size_type] number of images
    [off_t] offset to first signature in file
    for each bucket:
    [size_t] number of images in bucket
    for each image:
    [imageId] image id at this index
    ...hole in file until offset to first signature in file, to allow adding more image ids
    then follow image signatures, see struct ImgData
  */

  DEBUG(imgdb)("Saving dummy db... ");

  db_ofstream file(filename);

  file.write<int32_t>(SRZ_V_CODE);
  file.write<count_t>(m_images.size());

  offset_t firstOff = 0;
  firstOff += sizeof(int32_t) + sizeof(count_t) + sizeof(offset_t);
  firstOff += imgbuckets.size();
  firstOff += 1024 * sizeof(imageId); // leave space for 1024 new IDs

  file.write<offset_t>(firstOff);

  // save bucket sizes
  for (buckets_t::iterator itr = imgbuckets.begin(); itr != imgbuckets.end(); ++itr)
    file.write<count_t>(0);

  DEBUG_CONT(imgdb)(DEBUG_OUT, "done!\n");
}

// Relocate sigs from the end into the holes left by deleted images.
void dbSpaceAlter::move_deleted() {
  // need to find out which IDs are using the last few indices
  DeletedList::iterator delItr = m_deleted.begin();
  for (sigMap::iterator itr = m_images.begin();; ++itr) {
    // Don't fill holes that are beyond the new end!
    while (delItr != m_deleted.end() && *delItr >= m_images.size())
      ++delItr;

    if (itr == m_images.end() || delItr == m_deleted.end())
      break;

    if (itr->second < m_images.size())
      continue;

    ImgData sig = get_sig(itr->second);
    itr->second = *delItr++;
    m_f->seekp(m_sigOff + itr->second * sizeof(ImgData));
    m_f->write(sig);

    if (!m_rewriteIDs) {
      m_f->seekp(m_imgOff + itr->second * sizeof(imageId));
      m_f->write(sig.id);
    }
  }
  if (delItr != m_deleted.end())
    throw data_error("Not all deleted entries purged.");

  m_deleted.clear();

  // Truncate file here? Meh, it'll be appended again soon enough anyway.
}

void dbSpaceAlter::save_file(const char *filename) {
  if (!m_f)
    throw data_error("Couldn't save database; m_f is invalid");

  DEBUG(imgdb)("saving file, %zd deleted images... ", m_deleted.size());
  if (!m_deleted.empty())
    move_deleted();

  if (m_rewriteIDs) {
    DEBUG_CONT(imgdb)(DEBUG_OUT, "Rewriting all IDs... ");
    imageId_list ids(m_images.size(), ~imageId());
    for (sigMap::iterator itr = m_images.begin(); itr != m_images.end(); ++itr) {
      if (itr->second >= m_images.size())
        throw data_error("Invalid index on save.");
      if (ids[itr->second] != ~imageId())
        throw data_error("Duplicate index on save.");
      ids[itr->second] = itr->first;
    }
    // Shouldn't be possible.
    if (ids.size() != m_images.size())
      throw data_error("Image indices do not match images.");

    m_f->seekp(m_imgOff);
    m_f->write(&ids.front(), ids.size());
    m_rewriteIDs = false;
  }

  DEBUG_CONT(imgdb)(DEBUG_OUT, "saving header... ");
  m_f->seekp(0);
  m_f->write<uint32_t>(SRZ_V_CODE);
  m_f->seekp(m_hdrOff);
  m_f->write<count_t>(m_images.size());
  m_f->write(m_sigOff);
  m_f->write(m_buckets);

  DEBUG_CONT(imgdb)(DEBUG_OUT, "done!\n");
  m_f->flush();
}

// Need more space for image IDs in the header. Relocate first few
// image signatures to the end of the file and use the freed space
// for new image IDs until we run out of space again.
void dbSpaceAlter::resize_header() {
  // make space for 1024 new IDs
  const size_t numrel = (1024 * sizeof(imageId) + sizeof(ImgData) - 1) / sizeof(ImgData);
  DEBUG(imgdb)("relocating %zd/%zd images... from %llx ", numrel, m_images.size(), (long long int)m_sigOff);
  if (m_images.size() < numrel)
    throw internal_error("dbSpaceAlter::resize_header called with too few images!");
  ImgData sigs[numrel];
  m_f->seekg(m_sigOff);
  m_f->read(sigs, numrel);
  off_t writeOff = m_sigOff + m_images.size() * sizeof(ImgData);
  m_sigOff = m_f->tellg();
  DEBUG_CONT(imgdb)(DEBUG_OUT, "to %llx (new off %llx) ", (long long int)writeOff, (long long int)m_sigOff);
  m_f->seekp(writeOff);
  m_f->write(sigs, numrel);

  size_t addrel = m_images.size() - numrel;
  for (sigMap::iterator itr = m_images.begin(); itr != m_images.end(); ++itr)
    itr->second = (itr->second >= numrel ? itr->second - numrel : itr->second + addrel);
  DEBUG_CONT(imgdb)(DEBUG_OUT, "done.\n");

  m_rewriteIDs = true;
}

struct sim_result : public imageIterator::base_type {
  typedef typename imageIterator::base_type itr_type;
  sim_result(Score s, const itr_type &i) : itr_type(i), score(s) {}
  bool operator<(const sim_result &other) const { return score < other.score; }
  Score score;
};

inline bool dbSpaceImpl::skip_image(const imageIterator &itr) {
  return !itr.avgl().v[0];
}

sim_vector dbSpaceImpl::do_query(queryArg q, int num_colors) {
  int c;
  Score scale = 0;
  int sketch = 0;

  if (!m_bucketsValid)
    throw usage_error("Can't query with invalid buckets.");

  size_t count = m_nextIndex;
  std::vector<Score> scores(count, 0);

  // Luminance score (DC coefficient).
  for (imageIterator itr = image_begin(); itr != image_end(); ++itr) {
    Score s = 0;
#ifdef DEBUG_QUERY
    fprintf(stderr, "%zd:", itr.index());
#endif
    for (c = 0; c < num_colors; c++) {
#ifdef DEBUG_QUERY
      Score o = s;
      fprintf(stderr, " %d=%f*abs(%f-%f)", c, ScD(weights[sketch][0][c]), ScD(itr.avgl().v[c]), ScD(q.avgl.v[c]));
#endif
      s += DScSc(((DScore)weights[sketch][0][c]) * std::abs(itr.avgl().v[c] - q.avgl.v[c]));
#ifdef DEBUG_QUERY
      fprintf(stderr, "=%f", ScD(s - o));
#endif
    }
    scores[itr.index()] = s;
  }

#ifdef DEBUG_QUERY
  fprintf(stderr, "Lumi scores:");
  for (int i = 0; i < count; i++)
    fprintf(stderr, " %d=%f", i, ScD(scores[i]));
  fprintf(stderr, "\n");
#endif
#if QUERYSTATS
  size_t coefcnt = 0, coeflen = 0, coefmax = 0;
  size_t setcnt[NUM_COEFS * num_colors];
  std::vector<unit8_t> counts(count, 0);
  memset(setcnt, 0, sizeof(setcnt));
#endif
  int flag_fast = 0;
  int flag_nocommon = 0;
  for (int b = flag_fast ? NUM_COEFS : 0; b < NUM_COEFS; b++) { // for every coef on a sig
    for (c = 0; c < num_colors; c++) {
      int idx;
      bucket_type &bucket = imgbuckets.at(c, q.sig[c][b], &idx);
      if (bucket.empty())
        continue;
      if (flag_nocommon && bucket.size() > count / 10)
        continue;

      Score weight = weights[sketch][imgBin[idx]][c];
      //fprintf(stderr, "%d:%d=%d has %zd=%f\n", b, c, idx, bucket.size(), ScD(weight));
      scale -= weight;

      // update the score of every image which has this coef
      AutoImageIdIndex_map map(bucket.map_all());
#if QUERYSTATS
      size_t len = bucket.size();
      coeflen += len;
      coefmax = std::max(coefmax, len);
      coefcnt++;
#endif
      for (auto itr(map.begin()); itr != map.end(); ++itr) {
        scores[itr.get_index()] -= weight;
#if QUERYSTATS
        counts[itr.get_index()]++;
#endif
      }
      for (imageIdIndex_list::container::const_iterator itr(bucket.tail().begin()); itr != bucket.tail().end(); ++itr) {
        scores[itr.get_index()] -= weight;
#if QUERYSTATS
        counts[itr.get_index()]++;
#endif
      }
    }
  }
//fprintf(stderr, "Total scale=%f\n", ScD(scale));
#ifdef DEBUG_QUERY
  fprintf(stderr, "Final scores:");
  for (int i = 0; i < count; i++)
    fprintf(stderr, " %d=%f", i, ScD(scores[i]));
  fprintf(stderr, "\n");
#endif

  typedef std::priority_queue<sim_result> sigPriorityQueue;

  sigPriorityQueue pqResults; /* results priority queue; largest at top */

  imageIterator itr = image_begin();

  sim_vector V;

  typedef std::map<int, size_t> set_map;
  set_map sets;
  unsigned int need = q.numres;

  // Fill up the numres-bounded priority queue (largest at top):
  while (pqResults.size() < need && itr != image_end()) {
    if (skip_image(itr)) {
      ++itr;
      continue;
    }

#if QUERYSTATS
    setcnt[counts[itr.index()]]++;
#endif
    pqResults.push(sim_result(scores[itr.index()], itr));

    //imageIterator top(pqResults.top(), *this);fprintf(stderr, "Added id=%08lx score=%.2f set=%x, now need %d. Worst is id %08lx score %.2f set %x has %zd\n", itr.id(), (double)scores[itr.index()]/ScoreMax, itr.set(), need, top.id(), (double)pqResults.top().score/ScoreMax, top.set(), sets[top.set()]); }
    ++itr;
  }

  for (; itr != image_end(); ++itr) {
    // only consider if not ignored due to keywords and if is a better match than the current worst match
#if QUERYSTATS
    if (!skip_image(itr))
      setcnt[counts[itr.index()]]++;
#endif
    if (scores[itr.index()] < pqResults.top().score) {
      if (skip_image(itr))
        continue;

      // Make room by dropping largest entry:
      pqResults.pop();
      // Insert new entry:
      pqResults.push(sim_result(scores[itr.index()], itr));
    }
  }

  //fprintf(stderr, "Have %zd images in result set.\n", pqResults.size());
  if (scale != 0)
    scale = ((DScore)MakeScore(1)) * MakeScore(1) / scale;
  //fprintf(stderr, "Inverted scale=%f\n", ScD(scale));
  while (!pqResults.empty()) {
    const sim_result &curResTmp = pqResults.top();

    imageIterator itr(curResTmp, *this);
    //fprintf(stderr, "Candidate %08lx = %.2f, set %x has %zd.\n", itr.id(), ScD(curResTmp.score), itr.set(), sets[itr.set()]);
    V.push_back(sim_value(itr.id(), DScSc(((DScore)curResTmp.score) * 100 * scale)));
    //else fprintf(stderr, "Skipped!\n");
    pqResults.pop();
  }

#if QUERYSTATS
  size_t num = 0;
  for (size_t i = 0; i < sizeof(setcnt) / sizeof(setcnt[0]); i++)
    num += setcnt[i];
  DEBUG(imgdb)("Query complete, coefcnt=%zd coeflen=%zd coefmax=%zd numset=%zd/%zd\nCounts: ", coefcnt, coeflen, coefmax, num, m_images.size());
  num = 0;
  for (size_t i = sizeof(setcnt) / sizeof(setcnt[0]) - 1; i > 0 && num < 10; i--)
    if (setcnt[i]) {
      num++;
      DEBUG_CONT(imgdb)(DEBUG_OUT, "%zd=%zd; ", i, setcnt[i]);
    }
  DEBUG_CONT(imgdb)(DEBUG_OUT, "\n");
#endif
  std::reverse(V.begin(), V.end());
  //fprintf(stderr, "Returning %zd images, top score %f.\n", V.size(), ScD(V[0].score));
  return V;
}

inline sim_vector
dbSpaceImpl::queryImg(const queryArg &query) {
  if (is_grayscale(query.avgl))
    return do_query(query, 1);
  else
    return do_query(query, 3);
}

void dbSpaceImpl::removeImage(imageId id) {
  // Can't efficiently remove it from buckets, just mark it as
  // invalid and remove it from query results.
  m_info[find(id).index()].avgl.v[0] = 0;
  m_images.erase(id);
}

void dbSpaceAlter::removeImage(imageId id) {
  sigMap::iterator itr = find(id);
  m_deleted.push_back(itr->second);
  m_images.erase(itr);
}

template <typename B>
inline void dbSpaceCommon::bucket_set<B>::remove(const ImgData &nsig) {
  lumin_native avgl;
  SigStruct::avglf2i(nsig.avglf, avgl);
  for (int i = 0; 0 && i < NUM_COEFS; i++) {
    if (nsig.sig1[i] > 0)
      buckets[0][0][nsig.sig1[i]].remove(nsig.id);
    if (nsig.sig1[i] < 0)
      buckets[0][1][-nsig.sig1[i]].remove(nsig.id);

    if (is_grayscale(avgl))
      continue; // ignore I/Q coeff's if chrominance too low

    if (nsig.sig2[i] > 0)
      buckets[1][0][nsig.sig2[i]].remove(nsig.id);
    if (nsig.sig2[i] < 0)
      buckets[1][1][-nsig.sig2[i]].remove(nsig.id);

    if (nsig.sig3[i] > 0)
      buckets[2][0][nsig.sig3[i]].remove(nsig.id);
    if (nsig.sig3[i] < 0)
      buckets[2][1][-nsig.sig3[i]].remove(nsig.id);
  }
}

Score dbSpaceCommon::calcAvglDiff(imageId id1, imageId id2) {
  /* return the average luminance difference */

  // are images on db ?
  lumin_native avgl1, avgl2;
  getImgAvgl(id1, avgl1);
  getImgAvgl(id2, avgl2);
  return std::abs(avgl1.v[0] - avgl2.v[0]) + std::abs(avgl1.v[1] - avgl2.v[1]) + std::abs(avgl1.v[2] - avgl2.v[2]);
}

Score dbSpaceCommon::calcSim(imageId id1, imageId id2, bool ignore_color) {
  /* use it to tell the content-based difference between two images */
  ImgData dsig1, dsig2;
  getImgDataByID(id1, &dsig1);
  getImgDataByID(id2, &dsig2);

  Idx *const sig1[3] = {dsig1.sig1, dsig1.sig2, dsig1.sig3};
  Idx *const sig2[3] = {dsig2.sig1, dsig2.sig2, dsig2.sig3};

  Score score = 0, scale = 0;
  lumin_native avgl1, avgl2;
  image_info::avglf2i(dsig1.avglf, avgl1);
  image_info::avglf2i(dsig2.avglf, avgl2);

  int cnum = ignore_color || is_grayscale(avgl1) || is_grayscale(avgl2) ? 1 : 3;

  for (int c = 0; c < cnum; c++)
    score += DScSc(2 * ((DScore)weights[0][0][c]) * std::abs(avgl1.v[c] - avgl2.v[c]));

  for (int c = 0; c < cnum; c++) {
    std::sort(sig1[c] + 0, sig1[c] + NUM_COEFS);
    std::sort(sig2[c] + 0, sig2[c] + NUM_COEFS);

    for (int b1 = 0, b2 = 0; b1 < NUM_COEFS || b2 < NUM_COEFS;) {
      int ind1 = b1 == NUM_COEFS ? std::numeric_limits<int>::max() : sig1[c][b1];
      int ind2 = b2 == NUM_COEFS ? std::numeric_limits<int>::max() : sig2[c][b2];

      Score weight = weights[0][imgBin[std::abs(ind1 < ind2 ? ind1 : ind2)]][c];
      scale -= weight;

      if (ind1 == ind2)
        score -= weight;

      b1 += ind1 <= ind2;
      b2 += ind2 <= ind1;
    }
  }

  scale = ((DScore)MakeScore(1)) * MakeScore(1) / scale;
  return DScSc(((DScore)score) * 100 * scale);
}

void dbSpaceImpl::rehash() {
  throw usage_error("Not supported");
}

void dbSpaceAlter::rehash() {
  memset(m_buckets.begin(), 0, m_buckets.size());

  for (sigMap::iterator it = m_images.begin(); it != m_images.end(); ++it)
    m_buckets.add(get_sig(it->second), it->second);
}

size_t dbSpaceImpl::getImgCount() {
  return m_images.size();
}

size_t dbSpaceAlter::getImgCount() {
  return m_images.size();
}
imageId_list dbSpaceImpl::getImgIdList() {
  imageId_list ids;

  ids.reserve(getImgCount());
  for (imageIterator it = image_begin(); it != image_end(); ++it)
    if (!skip_image(it))
      ids.push_back(it.id());

  return ids;
}

imageId_list dbSpaceAlter::getImgIdList() {
  imageId_list ids;

  ids.reserve(m_images.size());
  for (sigMap::iterator it = m_images.begin(); it != m_images.end(); ++it)
    ids.push_back(it->first);

  return ids;
}

dbSpace::dbSpace() {}
dbSpace::~dbSpace() {}

dbSpaceImpl::dbSpaceImpl() : m_nextIndex(0), m_bucketsValid(true) {
  if (!imgBinInited)
    initImgBin();
  if (imgbuckets.count() != sizeof(imgbuckets) / sizeof(imgbuckets[0][0][0]))
    throw internal_error("bucket_set.count() is wrong!");
}

dbSpaceAlter::dbSpaceAlter(const char* filename) : m_f(NULL), m_rewriteIDs(false), m_readonly(false) {
  if (!imgBinInited)
    initImgBin();

  m_f = new db_fstream(filename);
}

dbSpaceImpl::~dbSpaceImpl() {
}

dbSpaceAlter::~dbSpaceAlter() {
  if (m_f && !m_readonly) {
    save_file(NULL);
    m_f->close();
    delete m_f;
    m_f = NULL;
    m_fname.clear();
  }
}

} // namespace imgdb
