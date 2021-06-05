cmake_minimum_required(VERSION 3.10)

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
find_package(Threads REQUIRED)

add_executable(iqdb debug.cpp haar.cpp imgdb.cpp iqdb.cpp resizer.cpp server.cpp vendor/httplib.cpp)
#add_executable(test-iqdb test-iqdb.cpp debug.cpp haar.cpp imgdb.cpp iqdb.cpp resizer.cpp server.cpp vendor/httplib.cpp)

#install(TARGETS iqdb DESTINATION bin)

find_package(PkgConfig)
pkg_check_modules(GDLIB REQUIRED gdlib)
target_link_libraries(iqdb Threads::Threads ${GDLIB_LIBRARIES})
target_include_directories(iqdb PUBLIC ${GDLIB_INCLUDE_DIRS})
target_compile_options(iqdb PUBLIC ${GDLIB_CFLAGS_OTHER})
