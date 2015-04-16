#ifndef __RVM_INTERNAL_H__
#define __RVM_INTERNAL_H__

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace std;

typedef void * rvm_t;
typedef void * trans_t;

class RVM {
	public:
		RVM(const char *dir): dir(dir) {}
		const char *get_segname(void *segbase);
		void *get_segbase(const char *segname);
		int get_size(const char *segname);

		string dir;
		unordered_map<string, int> segname_size_map;
		unordered_map<void *, const char *> segbase_segname_map;
		unordered_map<string, void *> segname_segbase_map;
		unordered_set<void *> being_modified_set;
};

class ChangeLog {
	public:
		ChangeLog(char *d, size_t o, size_t n): data(d+o, n), offset(o) {}

		string data;
		size_t offset;
};

class Transaction {
	public:
		Transaction(RVM *rvm): parent(rvm) {}

		unordered_map<void *, vector<ChangeLog>> logs;
		RVM *parent;
};

#endif