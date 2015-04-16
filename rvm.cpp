#include "rvm.h"
#include <string>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fstream>
#include <unistd.h>

using namespace std;

/* RVM class */
const char *RVM::get_segname(void *segbase) {
	return segbase_segname_map.count(segbase) ? segbase_segname_map[segbase] : NULL;
}

void *RVM::get_segbase(const char *segname) {
	string name(segname);
	return segname_segbase_map.count(name) ? segname_segbase_map[name] : NULL;
}

int RVM::get_size(const char *segname) {
	string name(segname);
	return segname_size_map.count(name) ? segname_size_map[name] : -1;
}

/* Initialize the library with the specified directory as backing store. */
rvm_t rvm_init(const char *directory) {
	if(!directory) return (rvm_t)-1;

	RVM *rvm = new RVM(directory);
	// S_IRWXU: read, write, execute/search by owner
	int ret = mkdir(directory, S_IRWXU);
	// EEXIST: the named file exists
	if(ret == -1 && errno != EEXIST) {
		delete rvm;
		rvm = (RVM*)-1;
	}
	return (rvm_t)rvm;
}

/* Map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice. */
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create) {
	if(rvm == (rvm_t)-1  || rvm == (rvm_t)NULL || !segname || size_to_create < 0) return (void *)-1;

	RVM *Rvm = (RVM *)rvm;

	if(!Rvm->segname_size_map.count(segname)) return (void *)-1;
	
	// open file
	string file_name = Rvm->dir + "/" + segname;
	fstream segment(file_name, fstream::in | fstream::out | fstream::binary);
	if(!segment) return (void *)-1;
	
	// get file size
	segment.seekg(0, segment.end);
	int filesize = segment.tellg();
	if(filesize == -1) {
		segment.close();
		return (void *)-1;
	}

	if(size_to_create != 0 && size_to_create > filesize) {
		segment.seekp(size_to_create-1, segment.beg);
		char c = '\0';
		segment.write(&c, sizeof(char));
		segment.flush();
	}
	else {
		size_to_create = filesize;
	}

	segment.seekg(0, segment.beg);
	char *buffer = new char[size_to_create];
	segment.read(buffer, size_to_create);

	string log_file_name = file_name + ".log";
	fstream log_file(log_file_name, fstream::in | fstream::out | fstream::binary);
	if(log_file) {
		size_t offset, length;
		bool log_exists = false;

		log_file.seekg(0, log_file.beg);
		while(log_file.read((char *)&offset, sizeof(size_t))) {
			log_exists = true;
			log_file.read((char *)&length, sizeof(size_t));
			log_file.read(buffer+offset, length);
		}

		log_file.close();

		if(log_exists) {
			segment.seekp(0, segment.beg);
			segment.write(buffer, size_to_create);

			log_file.open(log_file_name, fstream::trunc);
			log_file.close();
		}
	}

	segment.close();

	Rvm->segname_size_map[segname] = size_to_create;
	Rvm->segbase_segname_map[buffer] = segname;
	Rvm->segname_segbase_map[segname] = buffer;

	return buffer;
}

/* Unmap a segment from memory. */
void rvm_unmap(rvm_t rvm, void *segbase) {
	if(rvm == (rvm_t)-1 || rvm == (rvm_t)NULL) return;

	delete [] (char *)segbase;

	RVM *Rvm = (RVM *)rvm;
	Rvm->segname_size_map.erase(Rvm->get_segname(segbase));
	Rvm->segname_segbase_map.erase(Rvm->get_segname(segbase));
	Rvm->segbase_segname_map.erase(segbase);
}

/* Destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped. */
void rvm_destroy(rvm_t rvm, const char *segname) {
	if(rvm == (rvm_t)-1 || rvm == (rvm_t)NULL) return;

	RVM *Rvm = (RVM *)rvm;

	if(Rvm->segname_size_map.count(segname)) return;

	string file_name = Rvm->dir + "/" + segname;
	string log_file_name = file_name + ".log";
	unlink(file_name.c_str());
	unlink(log_file_name.c_str());
}

/* Begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trant_t needs to be able to be typecasted to an integer type. */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases) {
	if(rvm == (rvm_t)-1 || rvm == (rvm_t)NULL || numsegs <= 0 || segbases == NULL) return (trans_t)-1;

	RVM *Rvm = (RVM *)rvm;
	Transaction *Trans = new Transaction(Rvm);

	for(int i = 0; i < numsegs; ++i) {
		if(Rvm->segbase_segname_map.count(segbases[i])) {
			if(Rvm->being_modified_set.count(segbases[i])) {
				delete Trans;
				return (trans_t)-1;
			}

			Trans->logs[segbases[i]] = vector<ChangeLog>();
			Rvm->being_modified_set.insert(segbases[i]);
		}
		else {
			delete Trans;
			return (trans_t)-1;
		}
	}

	return Trans;
}

/* Declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area. */
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size) {
	if(tid == (trans_t)-1 || tid == (trans_t)NULL || offset < 0 || size < 0) return;

	Transaction *Trans = (Transaction *)tid;
	
	if(!Trans->logs.count(segbase)) return;
	ChangeLog change_log((char *)segbase, offset, size);
	Trans->logs[segbase].push_back(change_log);
}

/*  Commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts. */
void rvm_commit_trans(trans_t tid) {
	if(tid == (trans_t)-1 || tid == (trans_t)NULL) return;

	Transaction *Trans = (Transaction *)tid;
	RVM *Rvm = Trans->parent;

	for(auto ite = Trans->logs.begin(); ite != Trans->logs.end(); ++ite) {
		string file_name = Rvm->dir + "/" + Rvm->get_segname(ite->first);
		string log_file_name = file_name + ".log";

		fstream log_file(log_file_name, fstream::out | fstream::binary | fstream::trunc);

		if(log_file) {
			for(size_t i = 0; i < ite->second.size(); ++i) {
				size_t offset = ite->second[i].offset;
				size_t length = ite->second[i].data.size();
				log_file.write((char *)&offset, sizeof(size_t));
				log_file.write((char *)&length, sizeof(size_t));
				log_file.write((char *)ite->first + offset, length);
			}
		}

		log_file.close();
		Rvm->being_modified_set.erase(ite->first);
	}

	delete Trans;
}

/* Undo all changes that have happened within the specified transaction. */
void rvm_abort_trans(trans_t tid) {
	if(tid == (trans_t)-1 || tid == (trans_t)NULL) return;

	Transaction *Trans = (Transaction *)tid;
	RVM *Rvm = Trans->parent;

	for(auto ite = Trans->logs.begin(); ite != Trans->logs.end(); ++ite) {
		for(size_t i = ite->second.size()-1; i >= 0; --i) {
			size_t offset = ite->second[i].offset;
			string data = ite->second[i].data;
			memcpy((char *)ite->first + offset, data.c_str(), data.size());
		}
		Rvm->being_modified_set.erase(ite->first);
	}

	delete Trans;
}

/* Play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible. */
void rvm_truncate_log(rvm_t rvm) {
	if(rvm == (rvm_t)-1 || rvm == (rvm_t)NULL) return;

	RVM *Rvm = (RVM *)rvm;
	DIR *dirp = opendir(Rvm->dir.c_str());
	
	if(dirp) {
		dirent *dir;
		while((dir = readdir(dirp)) != NULL)  {
			string file_name = dir->d_name;
			size_t ext = file_name.rfind(".log");

			if(ext != string::npos) {
				string segname = file_name.substr(0, ext);
				string log_file_name = Rvm->dir + "/" + file_name;

				if(Rvm->segname_size_map.count(segname)) {
					void *buffer = Rvm->get_segbase(segname.c_str());
					int size = Rvm->get_size(segname.c_str());
					string seg_file_name = Rvm->dir + "/" + segname;
					
					fstream segment(seg_file_name, fstream::in | fstream::out | fstream::binary | fstream::trunc);
					fstream log_file(log_file_name, fstream::in | fstream::out | fstream::binary);
					
					if(log_file) {
						size_t offset, length;
						bool log_exists = false;

						log_file.seekg(0, log_file.beg);
						while(log_file.read((char *)&offset, sizeof(size_t))) {
							log_exists = true;
							log_file.read((char *)&length, sizeof(size_t));
							log_file.read((char *)buffer + offset, length);
						}

						log_file.close();

						if(log_exists) {
							segment.seekp(0, segment.beg);
							segment.write((char *)buffer, size);

							log_file.open(log_file_name, fstream::trunc);
							log_file.close();
						}
					}

					segment.close();
				}
				else {
					void *buffer = rvm_map(rvm, segname.c_str(), 0);
					if(buffer != (void *)-1) rvm_unmap(rvm, buffer);
				}

				unlink(log_file_name.c_str());
			}
		}
	}
}