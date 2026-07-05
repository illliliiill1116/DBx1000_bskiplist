#pragma once

#include "global.h"
#include "helper.h"
#include "index_base.h"

#define FOLLY_SKIPLIST_SCAN_BATCH 32
#define FOLLY_SKIPLIST_INSERT_LOCK_SHARDS 4096

struct FollySkiplistScanCursor {
	uint64_t 	keys[FOLLY_SKIPLIST_SCAN_BATCH];
	uint64_t 	vals[FOLLY_SKIPLIST_SCAN_BATCH];
	int 		count;	// number of valid entries currently in the buffer
	int 		pos;	// index of the next unread entry
};

class IndexFollySkiplist : public index_base
{
public:
	using index_base::init;
	RC 			init(int part_cnt);
	RC 			init(int part_cnt, table_t * table);
	~IndexFollySkiplist();
	bool 		index_exist(idx_key_t key);
	RC 			index_insert(idx_key_t key, itemid_t * item, int part_id=-1, uint64_t thd_id = 0);
	RC	 		index_read(idx_key_t key, itemid_t * &item,
							int part_id=-1, int thd_id=0);
	RC 			index_next(uint64_t thd_id, itemid_t * &item, bool samekey = false);

private:
	// really a std::shared_ptr<folly::ConcurrentSkipList<std::pair<uint64_t,
	// uint64_t>, KeyLess>>*, see index_folly_skiplist.cpp
	void * 			_skiplist;
	// really a std::mutex[FOLLY_SKIPLIST_INSERT_LOCK_SHARDS], see .cpp
	void * 			_insert_locks;
	char 			padding[PREFETCH_SIZE_BYTES];
	idx_key_t 		cur_key_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
	itemid_t * 		cur_item_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];

	FollySkiplistScanCursor scan_cursor_per_thd[THREAD_CNT];
};
