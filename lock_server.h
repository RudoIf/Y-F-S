// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

using namespace std;

class lock_server {

 protected:
  int nacquire;

 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);


  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);

 private:

  pthread_mutex_t lock_server_mutex;
  //pthread_cond_t  lock_server_cond;
  
  enum lockstate_t{
	  FREE = 0,	//a enum type will be initialized to 0 as map[keyword]
	  LOCKED
  };

	map<lock_protocol::lockid_t, lockstate_t>		lockstate_table;
	map<lock_protocol::lockid_t, pthread_cond_t>	lock_cond_table;
};

#endif 







