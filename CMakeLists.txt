cmake_minimum_required(VERSION 3.14)

project(iqdb)

# https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
# https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
# https://funroll-loops.oya.to/
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3 -Wall -Wextra -Wno-unused-parameter -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -march=x86-64 -DNDEBUG")
#set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -march=x86-64-v2 -DNDEBUG")

# Require C++17
# https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html
# https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD_REQUIRED.html
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# https://stackoverflow.com/questions/1620918/cmake-and-libpthread
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Compile httplib instead of using header-only version.
# https://github.com/yhirose/cpp-httplib/blob/master/CMakeLists.txt
set(HTTPLIB_COMPILE ON)

# Compile json library instead of using header-only version.
# https://github.com/nlohmann/json/blob/develop/CMakeLists.txt#L35
# https://github.com/nlohmann/json#integration
set(JSON_MultipleHeaders ON)

include(FetchContent)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
  GIT_TAG        v0.8.9
)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
  GIT_TAG v3.9.1
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v2.13.6
)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        7.1.3
)

FetchContent_MakeAvailable(httplib)
FetchContent_MakeAvailable(json)
FetchContent_MakeAvailable(Catch2)
FetchContent_MakeAvailable(fmt)
#FetchContent_MakeAvailable(sqliteOrm)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GDLIB REQUIRED gdlib)

add_subdirectory(src)
