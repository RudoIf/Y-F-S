// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


void*
lock_server_cache::retryer_thread(void* x){
	lock_server_cache *sc = (lock_server_cache *)x;
	sc->retryer();
	return 0;
}

void*
lock_server_cache::revoker_thread(void* x){
	lock_server_cache *sc = (lock_server_cache *)x;
	sc->revoker();
	return 0;
}

lock_server_cache::lock_server_cache()
{ 
	lock_server_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

	lock_server_cache_retryer_cond = PTHREAD_COND_INITIALIZER;
	lock_server_cache_revoker_cond = PTHREAD_COND_INITIALIZER;

	int r;
	pthread_t th;

	r = pthread_create(&th, NULL, &retryer_thread, (void *)this);
	r = pthread_create(&th, NULL, &revoker_thread, (void *)this);
	
	rpc_thr_pool = new ThrPool(20, false);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
  lock_protocol::status ret = lock_protocol::OK;
	int queue_len;
	struct lock *reqlock;
	pthread_mutex_lock(&lock_server_cache_mutex);
	
	if(lock_table.find(lid) == lock_table.end()){	//key not exist
		lock_table[lid].id  = lid;
		lock_table[lid].owner_cl.clear();
		//lock_table[lid].waiting_cls.empty(); it will initialize itself
	}
	
	reqlock	  = &(lock_table[lid]);
	queue_len = reqlock->waiting_cls.size();

	//if one client aquire, it will get OK or RETRY or revoke
	//according to the waiting queue
	if(reqlock->owner_cl.empty()){
		//printf("%s aquire fresh lock\n", id.c_str());
		ret = lock_protocol::OK;
		reqlock->owner_cl = id;
	}
	else{
		printf("lock in acquire: queue_len %d owner %s\n", queue_len, id.c_str());
		if(queue_len == 0){
			//revoke, let client return retry first.don't wait revoke
			revoker_queue.push(lid);
			pthread_cond_signal(&lock_server_cache_revoker_cond);
		}
		ret = lock_protocol::RETRY;
		reqlock->waiting_cls.push(id);	//once other holds, wait
		queue_len++;
	}

	pthread_mutex_unlock(&lock_server_cache_mutex);
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::IOERR;
	int queue_len;
	std::string retryone;
	std::map<lock_protocol::lockid_t, struct lock>::iterator itr;

	//printf("come to server's release\n");
	pthread_mutex_lock(&lock_server_cache_mutex);
	
	if((itr=lock_table.find(lid)) != lock_table.end()){	//key must exist
		if((itr->second).owner_cl == id){				//must be owner
			//printf("server's release begin to right release\n");
			ret = lock_protocol::OK;
			(itr->second).owner_cl.clear();
			retryer_queue.push(lid);
			//do retry, let the release return as soon as possible
			pthread_cond_signal(&lock_server_cache_retryer_cond);
					
		}	
	}
	pthread_mutex_unlock(&lock_server_cache_mutex);
	
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

void 
lock_server_cache::retryer(){

	lock_protocol::lockid_t retrylock_id;
	struct lock *retrylock;
	std::string retryclient;

	bool succ = false;

	pthread_mutex_lock(&lock_server_cache_mutex);
	while(true){
		while(retryer_queue.empty()){
			pthread_cond_wait(&lock_server_cache_retryer_cond, &lock_server_cache_mutex);
		}

		//printf("retryer awake in server\n");
		retrylock_id = retryer_queue.front();
		retrylock = &lock_table[retrylock_id];
		retryer_queue.pop();

		if(retrylock->waiting_cls.size() > 0){
			retryclient = retrylock->waiting_cls.front();
			//retrylock->waiting_cls.pop(); don't pop here, otherwise will cause double revoke
			retrylock->owner_cl = retryclient;
			
			succ = rpc_thr_pool->addObjJob(this, &lock_server_cache::retryer_rpc, retrylock);
			if(!succ){
				printf("threadpool add fail\n");
				succ = false;
			}	
				
		}

	}
	pthread_mutex_unlock(&lock_server_cache_mutex);
}

void
lock_server_cache::revoker(){
	lock_protocol::lockid_t revokelock_id;
	struct lock *revokelock;
	std::string revokeclient;

	bool succ;

	pthread_mutex_lock(&lock_server_cache_mutex);
	while(true){
		while(revoker_queue.empty()){
			pthread_cond_wait(&lock_server_cache_revoker_cond, &lock_server_cache_mutex);
		}
		revokelock_id = revoker_queue.front();
		revokelock = &lock_table[revokelock_id];
		revoker_queue.pop();

		//printf("revoker awake target:%s\n", revokelock->owner_cl.c_str());
		if(revokelock->waiting_cls.size() > 0){
			revokeclient = revokelock->owner_cl;
			//different with retry. The state should change later.
			//once you change here, I revoke chain will happan.
			succ = rpc_thr_pool->addObjJob(this, &lock_server_cache::revoker_rpc, revokelock);
			if(!succ){
				printf("threadpool add rebokerpc fail\n");
				succ = false;
			}		

		}
		else
			printf("revoke wrong, waiting_cls empty\n");

	}

}


void
lock_server_cache::retryer_rpc(struct lock *target){
	rlock_protocol::status ret;
	int r;

	//printf("thread:%d do retry lock:%d\n", pthread_self(), target->id);

	handle h(target->owner_cl);
	rpcc* cl = h.safebind();
	if(cl){
		ret = cl->call(rlock_protocol::retry, target->id, r);	
	}
	else{
		//printf("bind failed %s\n", retryclient.c_str());
		ret = rlock_protocol::RPCERR;
	}	

	pthread_mutex_lock(&lock_server_cache_mutex);
	
	if(ret != rlock_protocol::OK){
		//printf("retryer call rpc retry wrong\n");
		target->owner_cl.clear();
		retryer_queue.push(target->id);
	}
	else {
		target->waiting_cls.pop();
		if(!target->waiting_cls.empty()){
			//printf("retryer ask revoker\n");
			revoker_queue.push(target->id);
			pthread_cond_signal(&lock_server_cache_revoker_cond);
		}
	}

	pthread_mutex_unlock(&lock_server_cache_mutex);
}

void
lock_server_cache::revoker_rpc(struct lock *target){
	rlock_protocol::status ret;
	int r;

	//printf("thread:%d do revoke lock:%d\n", pthread_self(), target->id);
	
	handle h(target->owner_cl);
	rpcc* cl = h.safebind();
	if(cl){
		ret = cl->call(rlock_protocol::revoke, target->id, r);	
	}
	else{
		//printf("revoke bind failed %s\n", revokeclient.c_str());
		ret = rlock_protocol::RPCERR;
	}	

	pthread_mutex_lock(&lock_server_cache_mutex);
	
	if(ret != rlock_protocol::OK)
		revoker_queue.push(target->id);
	//revoke just tells the client to release 
	//release will truly release
	//retry is responsible for grant
	pthread_mutex_unlock(&lock_server_cache_mutex);
}










