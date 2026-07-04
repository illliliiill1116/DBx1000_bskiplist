#pragma once 

#include "global.h"
#include "helper.h"
#include "index_base.h"

extern "C" {
#include "OLC_bskiplist/include/olc_bskiplist.h"
}


#define OLC_BSKIPLIST_SCAN_BATCH 32

struct OLCBskiplistScanCursor {
	bsl_key_t 	keys[OLC_BSKIPLIST_SCAN_BATCH];
	bsl_val_t 	vals[OLC_BSKIPLIST_SCAN_BATCH];
	int 		count;	// number of valid entries currently in the buffer
	int 		pos;	// index of the next unread entry
};

class IndexOLCBskiplist : public index_base
{
public:
	using index_base::init;
	RC 			init(int part_cnt);
	RC 			init(int part_cnt, table_t * table);
	bool 		index_exist(idx_key_t key);
	RC 			index_insert(idx_key_t key, itemid_t * item, int part_id=-1, uint64_t thd_id = 0);
	RC	 		index_read(idx_key_t key, itemid_t * &item,
							int part_id=-1, int thd_id=0);
	RC 			index_next(uint64_t thd_id, itemid_t * &item, bool samekey = false);

private:
	bsl_t * 		_bsl_list;
	char 			padding[PREFETCH_SIZE_BYTES];
	idx_key_t 		cur_key_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
	itemid_t * 		cur_item_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];

	OLCBskiplistScanCursor scan_cursor_per_thd[THREAD_CNT];
};