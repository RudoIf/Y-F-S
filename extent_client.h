// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
 private:
  rpcc *cl;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status resize(extent_protocol::extentid_t eid, size_t new_size);
  extent_protocol::status getbuf(extent_protocol::extentid_t eid, size_t ask_size, size_t off,
				  std::string &buf);	//off_t wrong in <<
  extent_protocol::status putbuf(extent_protocol::extentid_t eid, size_t off, std::string buf);
};

#endif 

