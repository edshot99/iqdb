#ifndef IQDB_DEBUG_H
#define IQDB_DEBUG_H

#include <fmt/format.h>

extern int debug_level;

template<typename... Args>
inline void LOG(std::string format, int level, Args... args) {
  if (level >= debug_level) {
    fmt::print(stderr, format, args...); \
  }
}

template<typename... Args>
inline void DEBUG(std::string format, Args... args) {
  LOG("[debug] " + format, 0, args...);
}

template<typename... Args>
inline void ERROR(std::string format, Args... args) {
  LOG("[error] " + format, 1, args...);
}

template<typename... Args>
inline void WARN(std::string format, Args... args) {
  LOG("[warn] " + format, 2, args...);
}

template<typename... Args>
inline void INFO(std::string format, Args... args) {
  LOG("[info] " + format, 3, args...);
}

#endif // IQDB_DEBUG_H