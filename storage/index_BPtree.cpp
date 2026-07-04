#include "index_BPtree.h"
#include "mem_alloc.h"
#include "global.h"

#ifndef BPTREE_LEAFDS
#define BPTREE_LEAFDS 0
#endif

#include "container/btree_map.hpp"

using bptree_traits_t = tlx::btree_default_traits<uint64_t, uint64_t>;
using bptree_t = tlx::btree_map<uint64_t, uint64_t, std::less<uint64_t>,
								 bptree_traits_t, std::allocator<uint64_t>, true>;

static inline bptree_t * impl(void * p) { return (bptree_t *)p; }

RC IndexBPtree::init(int part_cnt) {
	_bptree = new bptree_t();
	M_ASSERT(_bptree != nullptr, "Failed to create BP-tree!");

	for (int i = 0; i < PREFETCH_SIZE_WORDS * THREAD_CNT; ++i) {
		cur_key_per_thd[i] = 0;
		cur_item_per_thd[i] = nullptr;
	}
	for (int t = 0; t < THREAD_CNT; ++t) {
		scan_cursor_per_thd[t].count = 0;
		scan_cursor_per_thd[t].pos = 0;
	}
	return RCOK;
}

RC IndexBPtree::init(int part_cnt, table_t * table) {
	this->table = table;
	return init(part_cnt);
}

IndexBPtree::~IndexBPtree() {
	delete impl(_bptree);
}

bool IndexBPtree::index_exist(idx_key_t key) {
	return impl(_bptree)->exists((uint64_t)key);
}

RC IndexBPtree::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
	auto res = impl(_bptree)->insert({(uint64_t)key, (uint64_t)item});
	if (res.second) {
		item->next = nullptr;
	}
	// if res.second == false, the tree already chained item->next onto the
	// previous head internally.
	return RCOK;
}

RC IndexBPtree::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
	auto val = impl(_bptree)->value((uint64_t)key);
	item = (itemid_t *)val;

	cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
	cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;

	scan_cursor_per_thd[thd_id].count = 0;
	scan_cursor_per_thd[thd_id].pos = 0;
	return RCOK;
}

RC IndexBPtree::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
	itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
	itemid_t * next_item = nullptr;
	if (last_item) {
		next_item = last_item->next;
	}
	if (!samekey && !next_item) {
		BPtreeScanCursor &sc = scan_cursor_per_thd[thd_id];

		if (sc.pos >= sc.count) {
			idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
			sc.count = 0;
#if BPTREE_LEAFDS
			impl(_bptree)->map_range_length((uint64_t)cur_key + 1, BPTREE_SCAN_BATCH,
				[&sc](auto k, auto v) {
					if (sc.count < BPTREE_SCAN_BATCH) {
						sc.keys[sc.count] = k;
						sc.vals[sc.count] = v;
						sc.count++;
					}
				});
#else
			impl(_bptree)->map_range_length((uint64_t)cur_key + 1, BPTREE_SCAN_BATCH,
				[&sc](auto el) {
					if (sc.count < BPTREE_SCAN_BATCH) {
						sc.keys[sc.count] = el.first;
						sc.vals[sc.count] = el.second;
						sc.count++;
					}
				});
#endif
			sc.pos = 0;
		}

		if (sc.pos < sc.count) {
			next_item = (itemid_t *)sc.vals[sc.pos];
			cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = (idx_key_t)sc.keys[sc.pos];
			sc.pos++;
		} else {
			next_item = nullptr;
		}
	}
	cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = next_item;
	item = next_item;
	return RCOK;
}
