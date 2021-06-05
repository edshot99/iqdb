#ifndef IQDB_DEBUG_H
#define IQDB_DEBUG_H

#include <cstdio>

extern int debug_level;

#define LOG(FMT, LEVEL, ...) \
  do { \
    if (debug_level >= LEVEL) { \
      fprintf(stderr, FMT, ## __VA_ARGS__); \
    } \
  } while (0)

#define DEBUG(fmt, ...) LOG("[debug] " fmt, 0, ## __VA_ARGS__)
#define ERROR(fmt, ...) LOG("[error] " fmt, 1, ## __VA_ARGS__)
#define WARN(fmt, ...)  LOG("[warn] "  fmt, 2, ## __VA_ARGS__)
#define INFO(fmt, ...)  LOG("[info] "  fmt, 3, ## __VA_ARGS__)

#endif // IQDB_DEBUG_H
