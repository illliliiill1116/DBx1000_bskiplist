#pragma once 

#include "global.h"
#include "helper.h"
#include "index_base.h"

#define MAXLEVEL 25
typedef struct SkiplistNode sl_node_t;
typedef sl_node_t * volatile sh_node_pt;

// each node contains items sharing the same key
struct SkiplistNode {
    uint64_t        		toplevel;
	idx_key_t 				key;
	itemid_t * volatile		items;
    sh_node_pt      		next[1];
};


// skiplist index does not support partition yet.
class IndexSkiplist  : public index_base
{
public:
	using index_base::init;
	RC 			init(int part_cnt);
	RC 			init(int part_cnt, table_t * table);
	bool 		index_exist(idx_key_t key); // check if the key exists.
	RC 			index_insert(idx_key_t key, itemid_t * item, int part_id=-1, uint64_t thd_id = 0);
	// the following call returns a single item
	RC	 		index_read(idx_key_t key, itemid_t * &item,
							int part_id=-1, int thd_id=0);
	RC 			index_next(uint64_t thd_id, itemid_t * &item, bool samekey = false);
private:
	// TODO: private funcs?
    static sl_node_t * 		allocate_node(unsigned int toplevel, idx_key_t key, int part_id = -1);
	unsigned int 			sl_randomLevel(const int tid);
    sl_node_t * 			weak_search_predecessors(idx_key_t key, sl_node_t **pa = nullptr, sl_node_t **na = nullptr);

    sl_node_t * 	head;
    uint64_t       	toplevel; // height of the tallest tower in the skiplist
	char 			padding[PREFETCH_SIZE_BYTES];
	sl_node_t * 	cur_node_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
	itemid_t * 		cur_item_per_thd[PREFETCH_SIZE_WORDS * THREAD_CNT];
};
