// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client_cache(lock_dst);
  srandom(getpid());

  printf("initialize: ec %x lc %x\n", ec, lc);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (lc->acquire(inum) == lock_protocol::OK){
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    lc->release(inum);
	goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);
 	lc->release(inum);
 }
  else
	return IOERR;

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (lc->acquire(inum) == lock_protocol::OK){
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
 	lc->release(inum);
	goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;
	lc->release(inum);
  }
  else
	r = IOERR;

 release:
  return r;
}

yfs_client::inum
yfs_client::new_inum(inum parent, bool isfile){
	inum new_inum;

	
	do
	{
		if(isfile)
			new_inum =	random() | 0x80000000;
		else
			new_inum =	random() & 0x7fffffff;
	}while(new_inum == parent || new_inum <= 1);


	return new_inum;
}

yfs_client::status
yfs_client::dir_add_entry(inum parent, std::string name, inum &entry_inum, bool isfile){
	//in lab2, do not have to worry about locking.
	//
	bool ifexist = false;
	inum tmpinum = 0;
	yfs_client::status		yfs_r = OK;
	extent_protocol::status ext_r = extent_protocol::IOERR;
	lock_protocol::status	loc_r = lock_protocol::IOERR;
	std::string buf;

	if((loc_r=lc->acquire(parent)) == lock_protocol::OK){	  
		if((ext_r=ec->get(parent, buf)) == extent_protocol::OK){
			//analysis the dir format
			ifexist = dir_find(&buf, name, tmpinum);

		
			if(!ifexist){
				std::ostringstream ostr;
				tmpinum = this->new_inum(parent, isfile);
				ostr<< name << ':' << tmpinum << std::endl;
				buf.append(ostr.str());
				ec->put(tmpinum, "");
				ec->put(parent , buf);
			}		
			else
				yfs_r = EXIST;
			entry_inum = tmpinum; 
		}

		lc->release(parent);
	}
	yfs_r = stat2yfs(yfs_r, ext_r, loc_r);	
	return yfs_r;
}


bool yfs_client::dir_find(const std::string *dirbuf, std::string name, 
							inum &findinum, size_t *bufoff, size_t *buflen){
	bool r = false;
	std::istringstream istr(*dirbuf);
	std::string line;
	std::string curname;
	std::size_t bufloff;	//every line in buf's offset
	std::size_t linelen;	//the length of line

	while(getline(istr, line)){
		bufloff = istr.tellg();
		if((linelen=line.length()) == 0)
			continue;
	
		if(!ent_read(line, curname, findinum)){
			r = false;
			break;
		}
		
		if(curname == name){
			r = true;
			if(buflen != NULL)
				*buflen = linelen;
			if(bufoff != NULL)
				*bufoff = bufloff-linelen-1;
			break;
		}
	}

	return r;
}

bool yfs_client::ent_read(std::string  dirline, std::string &entname, inum &entry_inum){
	//dirline : xxxx:xxxxxxxxxxxxxxxx\n
	
	std::string::size_type namepos;

	namepos = dirline.find(':');
	if(namepos == -1)
		return false;
	entname = dirline.substr(0, namepos);
	//get inum
	std::istringstream inum_str(dirline.substr(namepos+1));
	if(!(inum_str >> entry_inum))	//if inum not a number
		return false;

	return true;
}

yfs_client::status
yfs_client::create(inum parent, std::string name, inum &new_inum){
	
	printf("In create parent:%llu name:%s \n", parent, name.c_str());
	
	status r;
	r = dir_add_entry(parent, name, new_inum, true);
	return r;
}

yfs_client::status
yfs_client::remove(inum parent, std::string name){
	
	printf("In remove parent:%llu name:%s \n", parent, name.c_str());

	yfs_client::status		yfs_r = IOERR;
	extent_protocol::status ext_r = extent_protocol::IOERR;
	lock_protocol::status	loc_r = lock_protocol::IOERR;
	inum	findinum;

	std::string buf;
	
	if((loc_r=lc->acquire(parent)) == lock_protocol::OK){
		if((ext_r=ec->get(parent, buf)) == extent_protocol::OK){
			std::size_t bufloff;	//every line in buf's offset
			std::size_t linelen;	//the length of line
		
			if(dir_find(&buf, name, findinum, &bufloff, &linelen))
				if((loc_r=lc->acquire(findinum)) == lock_protocol::OK){
					if((ext_r=ec->remove(findinum)) == extent_protocol::OK){
						buf.erase(bufloff, linelen);
						ec->put(parent , buf);//remove line from dir
						yfs_r = OK;
					}
					else
						yfs_r = NOENT;
					lc->release(findinum);
				}
			
		}
		lc->release(parent);
	}

	yfs_r = stat2yfs(yfs_r, ext_r, loc_r);

	return yfs_r;
}

yfs_client::status
yfs_client::mkadir(inum parent, std::string name, inum &new_inum){
	
	status r;
	r = dir_add_entry(parent, name, new_inum, false);
	return r;
}

yfs_client::status
yfs_client::lookup(inum parent, std::string name, inum &findinum){
	
	printf("In lookup parent:%llu name:%s \n", parent, name.c_str());

	yfs_client::status		yfs_r = NOENT;
	extent_protocol::status ext_r = extent_protocol::IOERR;
	lock_protocol::status	loc_r = lock_protocol::IOERR;
	std::string buf;

	if((loc_r=lc->acquire(parent)) == lock_protocol::OK){	
		if((ext_r=ec->get(parent, buf)) == extent_protocol::OK){
			if(dir_find(&buf, name, findinum))
				yfs_r = OK;
		}
		lc->release(parent);
	}
	
	yfs_r = stat2yfs(yfs_r, ext_r, loc_r);
	return yfs_r;
}


yfs_client::status
yfs_client::readir(inum parent, std::vector<dirent> &dirents){
	
	yfs_client::status		yfs_r = NOENT;
	extent_protocol::status ext_r = extent_protocol::IOERR;	
	lock_protocol::status	loc_r = lock_protocol::OK;
	std::string buf;

	if((ext_r=ec->get(parent, buf)) == extent_protocol::OK){
		std::istringstream istr(buf);
		std::string line;
		dirent		lent;	//line entry
		
		printf("int lllllllllllllllllllllllllllllllllllll bufsize:%d\n", buf.size());
		while(getline(istr, line)){
			if(line.length() == 0)
				continue;
			if(!ent_read(line, lent.name, lent.inum)){
				yfs_r = NOENT;
				break;
			}
			dirents.push_back(lent);
		}
		yfs_r = OK;
	}
	yfs_r = stat2yfs(yfs_r, ext_r, loc_r);

	return yfs_r;

}


//should be in client. See lab5
yfs_client::status
yfs_client::resize(inum target, size_t new_size){
	yfs_client::status		yfs_r = NOENT;
	extent_protocol::status ext_r = extent_protocol::IOERR;
	lock_protocol::status	loc_r = lock_protocol::IOERR;
	std::string buf;

	if(new_size < 0)
		yfs_r = yfs_client::IOERR;
	else{
		if((loc_r=lc->acquire(target)) == lock_protocol::OK){	
			if((ext_r=ec->get(target, buf)) == extent_protocol::OK){
				buf.resize(new_size);
				ec->put(target,buf);
				yfs_r = OK;
			}
					
			lc->release(target);
		}
		yfs_r = stat2yfs(yfs_r, ext_r, loc_r);
	}

	return yfs_r;
}

yfs_client::status
yfs_client::read(inum target, char* buf, size_t size, off_t offset, size_t &getb){
	
	yfs_client::status		yfs_r = NOENT;
	extent_protocol::status ext_r;
	lock_protocol::status	loc_r;
	std::string tmp;
		
	printf("In read target:%llu \n", target);

	if((ext_r=ec->getbuf(target, size, offset, tmp)) ==  extent_protocol::OK){
		getb = tmp.size();
		memcpy(buf, tmp.c_str(), getb);
		printf("In read done  target:%llu getb:%d buf:%s \n", target, getb, buf);
		yfs_r = OK;
	}
	else
		status_switch(yfs_r,ext_r);

	return yfs_r;
}

yfs_client::status
yfs_client::write(inum target, const char* buf, size_t size, off_t offset, size_t &wrtb){

	printf("In write target:%llu buf:%s \n", target, buf);
	yfs_client::status		yfs_r = NOENT;
	extent_protocol::status ext_r = extent_protocol::IOERR;
	lock_protocol::status	loc_r = lock_protocol::IOERR;
	std::string tmp(buf, size);
		
	printf("In write check tmp:%s\n", tmp.c_str());
	if((loc_r=lc->acquire(target)) == lock_protocol::OK){
		if((ext_r=ec->putbuf(target, offset, tmp)) ==  extent_protocol::OK){
			wrtb = tmp.size();
			printf("In write done  target:%llu wrtb:%d buf:%s \n", target, wrtb, tmp.c_str());
			yfs_r = OK;
		}
		
		lc->release(target);
	}
	
	yfs_r = stat2yfs(yfs_r, ext_r, loc_r); 

	return yfs_r;

}



