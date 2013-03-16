// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include <queue>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include "sys/time.h"
#include "rpc/thr_pool.h"

// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
	
	enum lockstate_t{
		NONE = 0,
		FREE,
		LOCKED,
		ACQUIRING,
		RELEASING
	};
	struct cache_lock{
		lock_protocol::lockid_t id;
		lockstate_t state;
		pthread_t	owner;
		pthread_cond_t queue_cond;
		pthread_cond_t revoke_cond;	//different with queue_cond
		pthread_cond_t tmout_cond;	//timeout cond in releasing
		bool		ifrevoke;		//which let revoke thread do first
		bool		iftimout;
		bool		ifrlsreq;		//if releaser require
		int wait_threads;
		int q;
	};
	int timeout_value;
	std::map<lock_protocol::lockid_t, cache_lock>		cachelock_table;
	pthread_mutex_t lock_client_cache_mutex;	//every state change

	void releaser();	//one thread hold this loop.
	void releaser_rpc(struct cache_lock *);	//threadpools do this job

	static void* releaser_thread(void* x);
	pthread_cond_t	releaser_cond;
	std::queue<lock_protocol::lockid_t> releaser_queue;
	
	ThrPool* rpc_thr_pool;		//rpc threads poll

 public:
 


  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);

};


#endif
