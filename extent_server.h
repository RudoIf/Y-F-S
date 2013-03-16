// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"


class extent_server {

 public:
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);
  int resize(extent_protocol::extentid_t id, size_t new_size, int &);
  int getbuf(extent_protocol::extentid_t id, size_t ask_size, size_t off, std::string &);
  int putbuf(extent_protocol::extentid_t id, size_t off, std::string buf, int &);

 private:

	pthread_mutex_t extent_server_mutex;
	struct store{
		std::string buf;
		extent_protocol::attr a;
	};

	std::map<int, struct store>	store_table;


};

#endif 







