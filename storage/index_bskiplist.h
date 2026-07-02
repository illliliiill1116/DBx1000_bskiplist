#pragma once 

#include "global.h"
#include "helper.h"
#include "index_base.h"

extern "C" {
#include "bskiplist.h"
}

class IndexBskiplist : public index_base
{
public:
	using index_base::init;
	RC 			init(int part_cnt);
	RC 			init(int part_cnt, table_t * table);
	bool 		index_exist(idx_key_t key); // check if the key exists.
	RC 			index_insert(idx_key_t key, itemid_t * item, int part_id=-1, uint64_t thd_id = 0);
	RC	 		index_read(idx_key_t key, itemid_t * &item,
							int part_id=-1, int thd_id=0);
	RC 			index_next(uint64_t thd_id, itemid_t * &item, bool samekey = false);

private:
	bsl_t * 		_bsl_list;
	char 			padding[PREFETCH_SIZE_BYTES];
	idx_key_t 		cur_key_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
	itemid_t * 		cur_item_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
};
