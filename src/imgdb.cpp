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

#include <sys/mman.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <iqdb/debug.h>
#include <iqdb/imgdb.h>
#include <iqdb/imglib.h>
#include <iqdb/haar_signature.h>
#include <iqdb/sqlite_db.h>

namespace imgdb {

inline imageIterator dbSpaceImpl::find(imageId i) {
  auto itr = m_images.find(i);
  if (itr == m_images.end())
    throw invalid_id("Invalid image ID.");
  return imageIterator(itr, *this);
}

bool dbSpaceImpl::hasImage(imageId id) {
  return m_images.find(id) != m_images.end();
}

template <typename B>
inline void dbSpaceCommon::bucket_set<B>::add(const HaarSignature &nsig, count_t index) {
  for (int c = 0; c < nsig.num_colors(); c++) {
    for (int i = 0; i < NUM_COEFS; i++) {
      int coef = nsig.sig[c][i];
      int s = coef < 0;
      buckets[c][s][abs(coef)].add(index);
    }
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

void dbSpaceImpl::addImageData(imageId post_id, const HaarSignature& signature) {
  if (hasImage(post_id)) // image already in db
    throw duplicate_id("Image already in database.");

  size_t ind = m_nextIndex++;
  if (ind > m_info.size())
    throw internal_error("Index incremented too much!");
  if (ind == m_info.size()) {
    if (ind >= m_info.capacity())
      m_info.reserve(10 + ind + ind / 40);
    m_info.resize(ind + 1);
  }
  m_info.at(ind).id = post_id;
  image_info::avglf2i(signature.avglf, m_info[ind].avgl);
  m_images.add_index(post_id, ind);

  imgbuckets.add(signature, ind);
  sqlite_db_->addImage(post_id, signature);
}

void dbSpaceImpl::loadDatabase(std::string filename) {
  sqlite_db_ = std::make_unique<SqliteDB>(filename);
  m_nextIndex = 0;
  m_images.clear();
  m_info.clear();
  // imgbuckets.clear();

  sqlite_db_->eachImage([&](const auto& image, const auto& haar) {
    if ((size_t)image.id >= m_info.size()) {
      m_info.resize(image.id + 50000);
    }

    image_info info;
    info.id = image.post_id;
    info.avgl.v[0] = image.avglf1;
    info.avgl.v[1] = image.avglf2;
    info.avgl.v[2] = image.avglf3;

    m_info[image.id] = info;
    m_images[image.post_id] = image.id;
    imgbuckets.add(haar, image.id);

    if (image.id % 10000 == 0) {
      INFO("Loaded image %ld (post #%ld)...\n", image.id, image.post_id);
    }
  });
}

// XXX imageId is the iqdb id, not the post id
bool dbSpaceImpl::isDeleted(imageId id) {
  return !m_info[id].avgl.v[0];
}

sim_vector dbSpace::queryFromBlob(const std::string blob, int numres) {
  HaarSignature signature = HaarSignature::from_file_content(blob);
  return queryFromSignature(signature, numres);
}

sim_vector dbSpaceImpl::queryFromSignature(const HaarSignature &signature, size_t numres) {
  Score scale = 0;
  std::vector<Score> scores(m_info.size(), 0);
  std::priority_queue<sim_value> pqResults; /* results priority queue; largest at top */
  sim_vector V; /* output results */

  DEBUG("signature=%s\n", signature.to_string().c_str());
  DEBUG("json signature=%s\n", signature.to_json().c_str());

  // Luminance score (DC coefficient).
  for (size_t i = 0; i < scores.size(); i++) {
    auto image_info = m_info[i];
    Score s = 0;

    for (int c = 0; c < signature.num_colors(); c++) {
      s += weights[0][c] * std::abs(image_info.avgl.v[c] - signature.avglf[c]);
    }

    scores[i] = s;
  }

  for (int b = 0; b < NUM_COEFS; b++) { // for every coef on a sig
    for (int c = 0; c < signature.num_colors(); c++) {
      int idx;
      bucket_type &bucket = imgbuckets.at(c, signature.sig[c][b], &idx);

      if (bucket.empty())
        continue;

      const int w = imgBin.bin[idx];
      Score weight = weights[w][c];
      scale -= weight;

      for (auto index : bucket) {
        scores[index] -= weight;
      }
    }
  }

  // Fill up the numres-bounded priority queue (largest at top):
  size_t i = 0;
  for (; pqResults.size() < numres && i < scores.size(); i++) {
    if (!isDeleted(i))
      pqResults.emplace(i, scores[i]);
  }

  for (; i < scores.size(); i++) {
    if (!isDeleted(i) && scores[i] < pqResults.top().score) {
      pqResults.pop();
      pqResults.emplace(i, scores[i]);
    }
  }

  if (scale != 0)
    scale = 1.0 / scale;

  while (!pqResults.empty()) {
    auto value = pqResults.top();
    value.id = m_info[value.id].id; // XXX replace iqdb id with post id
    value.score = value.score * 100 * scale;

    V.push_back(value);
    pqResults.pop();
  }

  std::reverse(V.begin(), V.end());
  return V;
}

void dbSpaceImpl::removeImage(imageId post_id) {
  // Can't efficiently remove it from buckets, just mark it as
  // invalid and remove it from query results.
  m_info[find(post_id).index()].avgl.v[0] = 0;
  m_images.erase(post_id);
  sqlite_db_->removeImage(post_id);
}

template <typename B>
inline void dbSpaceCommon::bucket_set<B>::remove(const HaarSignature &nsig, imageId post_id) {
  for (int c = 0; c < nsig.num_colors(); c++) {
    for (int i = 0; i < NUM_COEFS; i++) {
      int coef = nsig.sig[c][i];
      int s = coef < 0;
      buckets[c][s][abs(coef)].remove(post_id);
    }
  }
}

/*
Score dbSpaceCommon::calcAvglDiff(imageId id1, imageId id2) {
  // return the average luminance difference

  // are images on db ?
  lumin_native avgl1, avgl2;
  getImgAvgl(id1, avgl1);
  getImgAvgl(id2, avgl2);
  return std::abs(avgl1.v[0] - avgl2.v[0]) + std::abs(avgl1.v[1] - avgl2.v[1]) + std::abs(avgl1.v[2] - avgl2.v[2]);
}

Score dbSpaceCommon::calcSim(imageId id1, imageId id2, bool ignore_color) {
  // use it to tell the content-based difference between two images
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
    score += DScSc(2 * ((DScore)weights[0][c]) * std::abs(avgl1.v[c] - avgl2.v[c]));

  for (int c = 0; c < cnum; c++) {
    std::sort(sig1[c] + 0, sig1[c] + NUM_COEFS);
    std::sort(sig2[c] + 0, sig2[c] + NUM_COEFS);

    for (int b1 = 0, b2 = 0; b1 < NUM_COEFS || b2 < NUM_COEFS;) {
      int ind1 = b1 == NUM_COEFS ? std::numeric_limits<int>::max() : sig1[c][b1];
      int ind2 = b2 == NUM_COEFS ? std::numeric_limits<int>::max() : sig2[c][b2];

      Score weight = weights[imgBin[std::abs(ind1 < ind2 ? ind1 : ind2)]][c];
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
*/

size_t dbSpaceImpl::getImgCount() {
  return m_info.size();
}

dbSpace::dbSpace() {}
dbSpace::~dbSpace() {}

dbSpaceImpl::dbSpaceImpl(std::string filename) : m_nextIndex(0), sqlite_db_(nullptr) {
  loadDatabase(filename);
}

dbSpaceImpl::~dbSpaceImpl() {
}

} // namespace imgdb
