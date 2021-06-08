#ifndef HAAR_SIGNATURE_H
#define HAAR_SIGNATURE_H

#include <string>
#include <iqdb/haar.h>

namespace imgdb {

struct ImgData;

struct HaarSignature {
  double avglf[3];           /* YIQ for position [0,0] */
  int16_t sig[3][NUM_COEFS]; /* YIQ positions with largest magnitude */

  HaarSignature() {};
  explicit HaarSignature(const ImgData& img_data);
  static HaarSignature from_file_content(const std::string blob);

  std::string to_string() const;
  std::string to_json() const;
  bool is_grayscale() const noexcept;
  int num_colors() const noexcept;
};

}

#endif
