#include "index_BTreeOLC.h"
#include "mem_alloc.h"
#include "global.h"

#include "BTreeOLC/BTreeOLC.h"

using btreeolc_t = btreeolc::BTree<uint64_t, uint64_t>;

static inline btreeolc_t * impl(void * p) { return (btreeolc_t *)p; }

RC IndexBTreeOLC::init(int part_cnt) {
	_btree = new btreeolc_t();
	M_ASSERT(_btree != nullptr, "Failed to create BTreeOLC!");

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

RC IndexBTreeOLC::init(int part_cnt, table_t * table) {
	this->table = table;
	return init(part_cnt);
}

IndexBTreeOLC::~IndexBTreeOLC() {
	delete impl(_btree);
}

bool IndexBTreeOLC::index_exist(idx_key_t key) {
	uint64_t val;
	return impl(_btree)->lookup((uint64_t)key, val);
}

RC IndexBTreeOLC::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
	item->next = nullptr;
	impl(_btree)->insert((uint64_t)key, (uint64_t)item);
	return RCOK;
}

RC IndexBTreeOLC::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
	uint64_t val = 0;
	bool found = impl(_btree)->lookup((uint64_t)key, val);
	item = found ? (itemid_t *)val : nullptr;

	cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
	cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;

	scan_cursor_per_thd[thd_id].count = 0;
	scan_cursor_per_thd[thd_id].pos = 0;
	return RCOK;
}

RC IndexBTreeOLC::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
	itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
	itemid_t * next_item = nullptr;
	if (last_item) {
		next_item = last_item->next;
	}
	if (!samekey && !next_item) {
		BTreeOLCScanCursor &sc = scan_cursor_per_thd[thd_id];

		if (sc.pos >= sc.count) {
			idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
			sc.count = (int)impl(_btree)->scan((uint64_t)cur_key + 1, BTREEOLC_SCAN_BATCH,
				(uint64_t *)sc.vals, (uint64_t *)sc.keys);
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
