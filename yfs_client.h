#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"
#include "lock_client_cache.h"

#define	status_switch(yfs,ext) yfs = ext



class yfs_client {
  extent_client *ec;
  lock_client_cache	*lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };


 private:
  static std::string filename(inum);
  static inum n2i(std::string);
	//lab2, methods
    
	status dir_add_entry(inum parent, std::string name, inum &entry_inum, bool isfile);	
	static inum new_inum(inum parent, bool isfile);
	 bool dir_find(const std::string *dirbuf, std::string name,
							inum &findinum, size_t *bufoff = NULL, size_t *buflen = NULL);
	inline bool	ent_read(std::string  dirline, std::string &name, inum &entry_inum); 

 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

	//In lab2,lab3, implement following operation
	status create(inum parent, std::string name, inum &new_inum); 
	status mkadir(inum parent, std::string name, inum &new_inum);
	status lookup(inum parent, std::string name, inum &findinum);
	status readir(inum parent, std::vector<dirent> &dirents);
	status remove(inum parent, std::string name); 
	status resize(inum target, size_t new_size);
	status read	 (inum target, char* buf, size_t size, off_t offset, size_t &getb);
	status write (inum target, const char* buf, size_t size, off_t offset, size_t &wrtb);
};

inline yfs_client::status stat2yfs(
			yfs_client::status yfs, extent_protocol::status ext, lock_protocol::status loc){
		if(loc != lock_protocol::OK)
			if(loc == lock_protocol::RETRY)
				return	yfs_client::IOERR;
			else
				return --loc;

		if(ext != extent_protocol::OK)
			return ext;

		return	yfs;
			
}

#endif 
