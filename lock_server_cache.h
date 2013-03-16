#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include <queue>

#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#include "rpc/thr_pool.h"

class lock_server_cache {
 private:
  int nacquire;

	struct lock{
		lock_protocol::lockid_t	id;
		std::string owner_cl;
		std::queue<std::string> waiting_cls;
	};
	std::map<lock_protocol::lockid_t, struct lock> lock_table;
	pthread_mutex_t lock_server_cache_mutex;

	std::queue<lock_protocol::lockid_t> retryer_queue;
	std::queue<lock_protocol::lockid_t> revoker_queue;

	pthread_cond_t	lock_server_cache_retryer_cond;
	pthread_cond_t	lock_server_cache_revoker_cond;
	
	ThrPool* rpc_thr_pool;		//rpc threads poll
	
	static void* retryer_thread(void* x);
	static void* revoker_thread(void* x);


	void retryer();	//a thread hold this loop
	void revoker();	//another thread hold this loop

	void retryer_rpc(struct lock *);	//threadpools do this job
	void revoker_rpc(struct lock *);

 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
  
};

#endif
