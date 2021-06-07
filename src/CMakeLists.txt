file(GLOB iqdb_SRC CONFIGURE_DEPENDS "*.h" "*.cpp")
add_executable(iqdb ${iqdb_SRC})

target_link_libraries(iqdb PRIVATE Threads::Threads nlohmann_json::nlohmann_json httplib::httplib ${GDLIB_LIBRARIES})
target_include_directories(iqdb PUBLIC ../include PRIVATE ${HTTPLIB_INCLUDE_DIR} ${GDLIB_INCLUDE_DIRS})
target_compile_options(iqdb PUBLIC ${GDLIB_CFLAGS_OTHER})
