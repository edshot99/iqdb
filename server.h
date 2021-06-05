#ifndef SERVER_H
#define SERVER_H

#include <string>
#include "imgdb.h"

void help();
void http_server(const std::string host, const int port, const std::string database_filename);

#endif
