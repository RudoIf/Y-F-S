// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {
	int a;
    extent_server_mutex = PTHREAD_MUTEX_INITIALIZER;
	put(1, "", a);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // You fill this in for Lab 2.
  //
		
	pthread_mutex_lock(&extent_server_mutex);
	
	//if(store_table.find(id) == store_table.end()){	
	//don't have to check if key not exist.map support so.
	store_table[id].buf	 = buf;
	store_table[id].a.size	= buf.size();

	unsigned int curtime = time(NULL);
	store_table[id].a.atime	= curtime;
	store_table[id].a.mtime = curtime;
	store_table[id].a.ctime = curtime;

	pthread_mutex_unlock(&extent_server_mutex);

	printf("in put, id %llu bufsize:%d\n", id, buf.size());	
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  // You fill this in for Lab 2.
	extent_protocol::xxstatus r;
	pthread_mutex_lock(&extent_server_mutex);
	if(store_table.find(id) == store_table.end())
		r = extent_protocol::NOENT;
	else{
		buf = store_table[id].buf;
		store_table[id].a.atime	= time(NULL);
		r = extent_protocol::OK;
	}
	pthread_mutex_unlock(&extent_server_mutex);
	printf("in get, id %llu bufsize:%d\n", id, buf.size());	
	return r;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
  
	extent_protocol::status r;
 	pthread_mutex_lock(&extent_server_mutex);
	if(store_table.find(id) == store_table.end()){
		a.size	= 0;
		a.atime = 0;  
		a.mtime = 0;  
		a.ctime = 0;  
		r = extent_protocol::NOENT;
	}
	else{
		a = store_table[id].a;
		r = extent_protocol::OK;
	}

	pthread_mutex_unlock(&extent_server_mutex);

	return r;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
	// You fill this in for Lab 2.
  //
	extent_protocol::xxstatus r;
 	pthread_mutex_lock(&extent_server_mutex);
	
	if(	store_table.find(id) == store_table.end())
		r = extent_protocol::NOENT;
	else{
		store_table.erase(id);
		r = extent_protocol::OK;
	}

	pthread_mutex_unlock(&extent_server_mutex);
	return r;
}

int extent_server::resize(extent_protocol::extentid_t id, size_t new_size, int &)
{
	extent_protocol::status r;
  	pthread_mutex_lock(&extent_server_mutex);
	
	if(store_table.find(id) == store_table.end())
		r = extent_protocol::NOENT;
	else{
		if(store_table[id].buf.size() > new_size)
			store_table[id].a.mtime = time(NULL);
		store_table[id].buf.resize(new_size);
		store_table[id].a.ctime = time(NULL);
		r = extent_protocol::OK;
	}

	pthread_mutex_unlock(&extent_server_mutex);
	return r;
}


int extent_server::getbuf(extent_protocol::extentid_t id, size_t ask_size, size_t off, std::string &buf)
{
	extent_protocol::status r;
	struct store *record = NULL;
	pthread_mutex_lock(&extent_server_mutex);
	if(store_table.find(id) == store_table.end())
		r = extent_protocol::NOENT;
	else{
		record = &store_table[id];
		size_t len = record->buf.size();
		size_t allowsize;
		if(off < len){
			allowsize = (len - off) > ask_size ? ask_size : (len - off);
			buf = record->buf.substr(off, allowsize);
			record->a.atime	= time(NULL);
			r = extent_protocol::OK;
		}
		else
			r = extent_protocol::IOERR;
	}
	pthread_mutex_unlock(&extent_server_mutex);
	return r;
}



int extent_server::putbuf(extent_protocol::extentid_t id, size_t off, std::string buf, int &)
{
	extent_protocol::status r;
	struct store *record = NULL;

	printf("in putbuf, id %llu bufsize:%d\n", id, buf.size());	
	pthread_mutex_lock(&extent_server_mutex);
	if(store_table.find(id) == store_table.end())
		r = extent_protocol::NOENT;
	else{
		record = &store_table[id];
		size_t len = record->buf.size();
		size_t end = off + buf.size();
		if(end > len)
			record->buf.resize(end);		//resize string 
		
		record->buf.replace(off, buf.size(), buf);
		record->a.mtime	= time(NULL);
		record->a.size  = record->buf.size();
		printf("record size %d\n", record->a.size);
		r = extent_protocol::OK;
		
	}
	pthread_mutex_unlock(&extent_server_mutex);
	return r;
}


