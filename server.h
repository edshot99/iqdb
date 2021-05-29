#ifndef SERVER_H
#define SERVER_H

#include <string>
#include "imgdb.h"

void help();
void rehash(const char *fn);
void stats(const char *fn);
void list(const char *fn);
void add(const char *fn);
void count(const char *fn);
void command(int numfiles, char **files);
void sim(const char *fn, imgdb::imageId id, int numres);
void query(const char *fn, const char *img, int numres, int flags);
void server(const char *hostport, int numfiles, char **files, bool listen2);
void http_server(const std::string host, const int port, const std::string database_filename);

#endif
