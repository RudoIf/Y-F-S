// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


int lock_client_cache::last_port = 0;

void*
lock_client_cache::releaser_thread(void* x){
	lock_client_cache *cc = (lock_client_cache *)x;
	cc->releaser();
	return 0;
}

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

	timeout_value = 120; //5s
	lock_client_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
    releaser_cond = PTHREAD_COND_INITIALIZER;

	rpc_thr_pool = new ThrPool(20, false);
	int r;
	pthread_t th;

	r = pthread_create(&th, NULL, &releaser_thread, (void *)this);

}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
	
	cache_lock  *reqlock;

	pthread_mutex_lock(&lock_client_cache_mutex);

	if(cachelock_table.find(lid) == cachelock_table.end()){	//key not exist
		cachelock_table[lid].id = lid;
		reqlock = &(cachelock_table[lid]);
		reqlock->state = NONE;
		reqlock->owner = 0;
		reqlock->queue_cond = PTHREAD_COND_INITIALIZER;
		reqlock->revoke_cond = PTHREAD_COND_INITIALIZER;
		reqlock->tmout_cond = PTHREAD_COND_INITIALIZER;
		reqlock->ifrevoke = false;
		reqlock->iftimout = false;
		reqlock->ifrlsreq = false;
		reqlock->wait_threads = 0;
	}
	else
		reqlock = &(cachelock_table[lid]);
	
	reqlock->wait_threads++;

	//printf("aquire lock in client:%s wait:%d state:%d\n",
	//				id.c_str() ,reqlock->wait_threads, reqlock->state);

	switch(reqlock->state){	
		case lock_client_cache::LOCKED:
			if(reqlock->owner == pthread_self()){
				ret = lock_protocol::OK;
				break;
			}
RETRY_WAIT:
		case lock_client_cache::ACQUIRING:
		case lock_client_cache::RELEASING:
			while(	reqlock->state != lock_client_cache::FREE &&
					reqlock->state != lock_client_cache::NONE) //releasing will be none
				pthread_cond_wait(&reqlock->queue_cond, &lock_client_cache_mutex);
			//here is very tricky, if thread awake and state is NONE
			//that's means the releaser awake it. Then do NONE
			//Otherwise it will die here
			if(reqlock->state != NONE){
				ret = lock_protocol::OK;
				break;
			}
		case lock_client_cache::NONE:
			reqlock->state = lock_client_cache::ACQUIRING;
			//don't let rpc in one mutex
			pthread_mutex_unlock(&lock_client_cache_mutex);
			ret = cl->call(lock_protocol::acquire, lid, id, (reqlock->q));	
			pthread_mutex_lock	(&lock_client_cache_mutex);

			if(ret == lock_protocol::RETRY)
				goto RETRY_WAIT;		//only use goto here, to control flow in switch
			ret = lock_protocol::OK;
			break;
		case lock_client_cache::FREE:
			ret = lock_protocol::OK; 
			break;

	}

	if(ret == lock_protocol::OK){//only one situation, ret will not be OK. That's RPC FAIL.
		reqlock->state = lock_client_cache::LOCKED;
		reqlock->owner = pthread_self();	//everytime acquire a lock
		reqlock->wait_threads--;
	}
	pthread_mutex_unlock(&lock_client_cache_mutex);

  return ret;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	int ret = lock_protocol::NOENT;
	std::map<lock_protocol::lockid_t, cache_lock>::iterator itr;
	pthread_mutex_lock(&lock_client_cache_mutex);
	if((itr=cachelock_table.find(lid)) != cachelock_table.end()){
		if((itr->second).state == LOCKED && (itr->second).owner == pthread_self()){
			(itr->second).state = FREE;
			//printf("release in client %s\n", id.c_str());
			if(	(itr->second).iftimout || 
				((itr->second).ifrlsreq && (itr->second).wait_threads == 0) //ask release and wait 0
					)
				pthread_cond_signal(&(itr->second).tmout_cond);
			else if((itr->second).ifrevoke)
				pthread_cond_signal(&(itr->second).revoke_cond);	//let revoke go first
			else
				pthread_cond_signal(&(itr->second).queue_cond);

		
			ret = lock_protocol::OK;


		}
	}

	pthread_mutex_unlock(&lock_client_cache_mutex);
	return ret;

}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::RPCERR;
	
	std::map<lock_protocol::lockid_t, cache_lock>::iterator itr;
	pthread_mutex_lock(&lock_client_cache_mutex);
	if((itr=cachelock_table.find(lid)) != cachelock_table.end()){
		(itr->second).ifrevoke = true;

		//printf("revoke wait in client:%s state:%d\n",id.c_str(), (itr->second).state);
		while((itr->second).state != lock_client_cache::FREE)	
			pthread_cond_wait(&(itr->second).revoke_cond, &lock_client_cache_mutex);	
		//if revoke comes before returning OK, revoke should wait th
		
		//printf("revoke releasing in client:%s\n", id.c_str());
		ret = rlock_protocol::OK;
		(itr->second).ifrevoke = false;
		(itr->second).state = RELEASING;	//should not use cl here.
		releaser_queue.push(lid);
		pthread_cond_signal(&releaser_cond);
	}

	pthread_mutex_unlock(&lock_client_cache_mutex);
	return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::RPCERR;
	std::map<lock_protocol::lockid_t, cache_lock>::iterator itr;
	pthread_mutex_lock(&lock_client_cache_mutex);
	if((itr=cachelock_table.find(lid)) != cachelock_table.end()){
		if((itr->second).state == ACQUIRING){	//should be ACQURING and EXISTING in cache
			//printf("retry in client:%s\n", id.c_str());
			(itr->second).state = FREE;
			ret = rlock_protocol::OK;
			//it's OK, the situation retry_handler before returning RTRY
			pthread_cond_signal(&(itr->second).queue_cond);
		}
	}

	pthread_mutex_unlock(&lock_client_cache_mutex);
	return ret;
}

void 
lock_client_cache::releaser(){

	lock_protocol::lockid_t rlslock_id;
	struct cache_lock *rlslock;

	bool succ = false;

	pthread_mutex_lock(&lock_client_cache_mutex);
	while(true){
		while(releaser_queue.empty()){
			pthread_cond_wait(&releaser_cond, &lock_client_cache_mutex);
		}

		//printf("releaser wakeup in client %s\n", id.c_str());

		rlslock_id = releaser_queue.front();
		rlslock = &cachelock_table[rlslock_id];
		releaser_queue.pop();

		succ = rpc_thr_pool->addObjJob(this, &lock_client_cache::releaser_rpc, rlslock);
		if(!succ){
			printf("threadpool add releaser_rpc fail\n");
			succ = false;
		}		
			
	}
		
}


void
lock_client_cache::releaser_rpc(struct cache_lock *rlslock){
	rlock_protocol::status ret;
	int r;
	int wait_len;
	struct timespec outtime;

	pthread_mutex_lock(&lock_client_cache_mutex);

	wait_len = rlslock->wait_threads;


	VERIFY(rlslock->state == RELEASING);

	//printf("client:%s thread:%d lockid:%d wait_len:%d\n", 
	//			id.c_str(), pthread_self(), rlslock->id, wait_len);
	if(wait_len > 0){
		rlslock->state = FREE;
		rlslock->ifrlsreq = true;
		do{
			outtime.tv_sec = time(0) + timeout_value;
			outtime.tv_nsec = 0;
			pthread_cond_timedwait(&rlslock->tmout_cond, &lock_client_cache_mutex, &outtime);	
			rlslock->iftimout = true;
		}while(rlslock->state != lock_client_cache::FREE);
	}
	rlslock->iftimout = false;
	rlslock->ifrlsreq = false;
	rlslock->state = RELEASING;
	
	pthread_mutex_unlock(&lock_client_cache_mutex);
	//release rpc is sure to be done
	ret = cl->call(lock_protocol::release, rlslock->id, id, (rlslock->q));	
	pthread_mutex_lock(&lock_client_cache_mutex);
	
	VERIFY(rlslock->state == RELEASING);
	if(ret == lock_protocol::OK){
		//printf("releaser:%s return OK\n", id.c_str());
		rlslock->owner = 0;
		//rlslock->ifrevoke = false; must be false
		rlslock->state = NONE;
		if(rlslock->wait_threads > 0){	//awake the thread wait on RELEASING
			//printf("wake up thread waiting on lock\n");
			pthread_cond_signal(&rlslock->queue_cond);
		}
	}
	else
		releaser_queue.push(rlslock->id);	

	pthread_mutex_unlock(&lock_client_cache_mutex);
}



