// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
 
    lock_server_mutex = PTHREAD_MUTEX_INITIALIZER;

}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	
	lock_protocol::status ret = lock_protocol::OK;
	
	pthread_mutex_lock(&lock_server_mutex);
	if(lockstate_table.find(lid) == lockstate_table.end()){	//key not exist
		lockstate_table[lid] = FREE;
		lock_cond_table[lid] = PTHREAD_COND_INITIALIZER;
	}
	//printf("check if exist\n";
	while(lockstate_table[lid] != FREE) //a non-existed key's value is default 0 in map
		pthread_cond_wait(&lock_cond_table[lid],&lock_server_mutex);
	//printf("go to change state\n");
	lockstate_table[lid] = LOCKED;		//change states to LOCKED
	pthread_mutex_unlock(&lock_server_mutex);
		
	r = 0;			//value r doesn't matter
	return ret;		//one thread returns and means client gets the lock.So OK
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  if(lockstate_table.find(lid) != lockstate_table.end()){	//key should exist
		lockstate_table[lid] = FREE;
		pthread_cond_signal(&lock_cond_table[lid]);
  }
  r = 0;
  return ret;
}
