#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <iqdb/imgdb.h>

namespace iqdb {

void help();
void http_server(const std::string host, const int port, const std::string database_filename);

}

#endif
