#ifndef HAAR_SIGNATURE_H
#define HAAR_SIGNATURE_H

#include <string>
#include <iqdb/haar.h>

namespace imgdb {

using lumin_t = double[3];
using signature_t = int16_t[3][NUM_COEFS];

struct HaarSignature {
  lumin_t avglf;    // YIQ for position [0,0]
  signature_t sig;  // YIQ positions with largest magnitude

  HaarSignature() {};
  explicit HaarSignature(lumin_t avglf, signature_t sig);
  static HaarSignature from_file_content(const std::string blob);

  std::string to_string() const;
  std::string to_json() const;
  bool is_grayscale() const noexcept;
  int num_colors() const noexcept;
};

}

#endif
