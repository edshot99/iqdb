cmake_minimum_required(VERSION 3.14)
include(FetchContent)

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

# Compile library instead of using header-only version.
# https://github.com/nlohmann/json/blob/develop/CMakeLists.txt#L35
# https://github.com/nlohmann/json#integration
set(JSON_MultipleHeaders ON)

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

FetchContent_MakeAvailable(httplib)
FetchContent_MakeAvailable(json)
FetchContent_MakeAvailable(Catch2)
#FetchContent_MakeAvailable(sqliteOrm)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GDLIB REQUIRED gdlib)

add_executable(iqdb debug.cpp haar.cpp imgdb.cpp iqdb.cpp resizer.cpp server.cpp haar_signature.cpp)
#add_executable(test-iqdb test-iqdb.cpp debug.cpp haar.cpp imgdb.cpp iqdb.cpp resizer.cpp server.cpp vendor/httplib.cpp)

target_link_libraries(iqdb PRIVATE Threads::Threads nlohmann_json::nlohmann_json httplib::httplib ${GDLIB_LIBRARIES})
target_include_directories(iqdb PRIVATE ${HTTPLIB_INCLUDE_DIR} ${GDLIB_INCLUDE_DIRS})
target_compile_options(iqdb PUBLIC ${GDLIB_CFLAGS_OTHER})
