/***************************************************************************\
    resizer.cpp - Image resizer using libgd, using pre-scaled images
		  from libjpeg/libpng to be faster and use less memory.

    Copyright (C) 2008 piespy@gmail.com

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

#include <errno.h>
#include <stdio.h>

#include <arpa/inet.h> // For ntoh*

extern "C" {
#include <jerror.h>
#include <jpeglib.h>
}

#define PNG_USE_GLOBAL_ARRAYS
#include <png.h>

#define DEBUG_RESIZER
#include "debug.h"

#include "imgdb.h"
#include "resizer.h"

extern int debug_level;

inline int get_jpeg_info(const unsigned char *data, size_t length, image_info *info) {
  while (1) {
    if (length < 2)
      return -2;

    if (data[0] != 0xff || data[1] < 0xc0) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "nope, marker is %02x%02x.\n", data[0], data[1]);
      return 0;
    }

    // RST0..RST7 have no length value.
    if (data[1] >= 0xd0 && data[1] <= 0xd7) {
      data += 2;
      length -= 2;
      continue;
    }

    // SOF markers are what we are looking for.
    switch (data[1]) {
    case 0xc0:
    case 0xc1:
    case 0xc2:
    case 0xc3:
    case 0xc5:
    case 0xc6:
    case 0xc7:
    case 0xc9:
    case 0xca:
    case 0xcb:
    case 0xcd:
    case 0xce:
    case 0xcf:
    case 0xf7:
      if (length < 9) {
        DEBUG_CONT(image_info)(DEBUG_OUT, "too short to tell.\n");
        return 9 - length;
      }
      info->height = ntohs(*(uint16_t *)((data + 5)));
      info->width = ntohs(*(uint16_t *)((data + 7)));
      info->type = IMG_JPEG;
      info->mime_type = "image/jpeg";
      DEBUG_CONT(image_info)(DEBUG_OUT, "yes, %dx%d\n", info->width, info->height);
      return 0;
    }

    // Otherwise skip block.
    size_t blen = length < 2 ? 2 : ntohs(*(uint16_t *)((data + 2)));
    if (length < blen + 4) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "too short to tell.\n");
      return blen + 4 - length;
    }
    data += blen + 2;
    length -= blen + 2;
  }
}

inline uint32_t get_le_32(const unsigned char *data) {
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}
inline uint16_t get_le_16(const unsigned char *data) {
  return data[0] | (data[1] << 8);
}

size_t get_image_info(const unsigned char *data, size_t length, image_info *info) {
  DEBUG(image_info)("Determining image info for %zd bytes at %p... ", length, data);
  info->type = IMG_UNKNOWN;
  info->mime_type = "application/octet-stream";

  if (length < 10) {
    DEBUG_CONT(image_info)(DEBUG_OUT, "too short to tell.\n");
    return 10 - length;
  }

  if (data[0] == 0xff && data[1] == 0xd8) {
    DEBUG_CONT(image_info)(DEBUG_OUT, "looks like JPEG... ");
    data += 2;
    length -= 2;
    return get_jpeg_info(data, length, info);

  } else if (!memcmp(data, "\x89PNG\x0D\x0A\x1A\x0A", 8)) {
    DEBUG_CONT(image_info)(DEBUG_OUT, "looks like PNG... ");
    data += 8;
    length -= 8;
    if (length < 16) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "too short to tell.\n");
      return 16 - length;
    }
    if (memcmp(data, "\0\0\0\x0dIHDR", 8)) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "nope, no IHDR chunk.\n");
      return 0;
    }
    info->width = ntohl(*(uint32_t *)((data + 8)));
    info->height = ntohl(*(uint32_t *)((data + 12)));
    info->type = IMG_PNG;
    info->mime_type = "image/png";
    DEBUG_CONT(image_info)(DEBUG_OUT, "yes, %dx%d\n", info->width, info->height);
    return 0;

  } else if (!memcmp(data, "GIF", 3)) {
    DEBUG_CONT(image_info)(DEBUG_OUT, "looks like GIF... ");
    data += 6;
    length -= 6;
    info->width = get_le_16(data);
    info->height = get_le_16(data + 2);
    info->type = IMG_GIF;
    info->mime_type = "image/gif";
    DEBUG_CONT(image_info)(DEBUG_OUT, "yes, %dx%d\n", info->width, info->height);
    return 0;

  } else if (data[0] == 'B' && data[1] == 'M') {
    DEBUG_CONT(image_info)(DEBUG_OUT, "looks like BMP... ");
    if (length < 26) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "too short to tell.\n");
      return 26 - length;
    }
    data += 14;
    length -= 14;
    if (get_le_32(data) != 40) {
      DEBUG_CONT(image_info)(DEBUG_OUT, "nope, wrong header size.\n");
      return 0;
    }
    info->width = get_le_32(data + 4);
    info->width = get_le_32(data + 8);
    info->type = IMG_BMP;
    info->mime_type = "image/bmp";
    DEBUG_CONT(image_info)(DEBUG_OUT, "yes, %dx%d\n", info->width, info->height);
    return 0;
  }

  DEBUG_CONT(image_info)(DEBUG_OUT, "doesn't look like anything.\n");
  return 0;
}

resizer_result resize_image_data(const unsigned char *data, size_t len, unsigned int thu_x, unsigned int thu_y) {
  image_info info;
  get_image_info(data, len, &info);

  DEBUG(resizer)("Is %s %d x %d.\n", info.mime_type, info.width, info.height);

  if (info.type != IMG_JPEG)
    throw imgdb::image_error("Unsupported image format.");

  AutoCleanPtrF<gdImage, &gdImageDestroy> thu(gdImageCreateTrueColor(thu_x, thu_y));
  if (!thu)
    throw imgdb::simple_error("Out of memory.");

  AutoCleanPtrF<gdImage, &gdImageDestroy> img;
  img.set(gdImageCreateFromJpegPtr(len, const_cast<unsigned char *>(data)));
  if (!img)
    throw imgdb::image_error("Could not read image.");

  if ((unsigned int)img->sx == thu_x && (unsigned int)img->sy == thu_y)
    return img.detach();

  gdImageCopyResampled(thu, img, 0, 0, 0, 0, thu_x, thu_y, img->sx, img->sy);
  DEBUG(terse)("Resized %s %d x %d via %d x %d to %d x %d.\n", info.mime_type, info.width, info.height, img->sx, img->sy, thu_x, thu_y);

  // Stop autocleaning thu, and return its value instead.
  return resizer_result(thu.detach(), img->sx, img->sy);
}
