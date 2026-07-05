#include "index_folly_skiplist.h"
#include "mem_alloc.h"
#include "global.h"

#include <mutex>
#include <utility>

#include <folly/ConcurrentSkipList.h>

namespace {

struct dbx1000_itemid_layout {
	int type;
	void *location;
	void *next;
	bool valid;
};
inline void dbx1000_chain_onto(uint64_t old_value, uint64_t new_value) {
	((dbx1000_itemid_layout *)new_value)->next = (void *)old_value;
}

using KV = std::pair<uint64_t, uint64_t>;

struct KeyLess {
	bool operator()(const KV &a, const KV &b) const { return a.first < b.first; }
};

using SkipListT = folly::ConcurrentSkipList<KV, KeyLess>;
using SkipListPtr = std::shared_ptr<SkipListT>;
using Accessor = SkipListT::Accessor;
using Skipper = SkipListT::Skipper;

constexpr int SKIPLIST_HEIGHT = 24;

}

static inline SkipListPtr * impl(void * p) { return (SkipListPtr *)p; }
static inline std::mutex * locks(void * p) { return (std::mutex *)p; }

RC IndexFollySkiplist::init(int part_cnt) {
	_skiplist = new SkipListPtr(SkipListT::createInstance(SKIPLIST_HEIGHT));
	M_ASSERT(_skiplist != nullptr, "Failed to create folly ConcurrentSkipList!");

	_insert_locks = new std::mutex[FOLLY_SKIPLIST_INSERT_LOCK_SHARDS];
	M_ASSERT(_insert_locks != nullptr, "Failed to allocate insert locks!");

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

RC IndexFollySkiplist::init(int part_cnt, table_t * table) {
	this->table = table;
	return init(part_cnt);
}

IndexFollySkiplist::~IndexFollySkiplist() {
	delete impl(_skiplist);
	delete[] locks(_insert_locks);
}

bool IndexFollySkiplist::index_exist(idx_key_t key) {
	Accessor accessor(*impl(_skiplist));
	return accessor.contains({(uint64_t)key, 0});
}

RC IndexFollySkiplist::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
	// Unlike BP-tree/BTreeOLC/RW_bskiplist, this duplicate-key chain fix
	// can't live inside insert() itself: folly::ConcurrentSkipList is a
	// lock-free set of *unique* keys by design.
	std::mutex &lock = locks(_insert_locks)[std::hash<uint64_t>{}((uint64_t)key) % FOLLY_SKIPLIST_INSERT_LOCK_SHARDS];
	std::lock_guard<std::mutex> guard(lock);

	Accessor accessor(*impl(_skiplist));
	auto it = accessor.find({(uint64_t)key, 0});
	if (it != accessor.end()) {
		dbx1000_chain_onto(it->second, (uint64_t)item);
		it->second = (uint64_t)item;
	} else {
		item->next = nullptr;
		accessor.insert(KV{(uint64_t)key, (uint64_t)item});
	}
	return RCOK;
}

RC IndexFollySkiplist::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
	Accessor accessor(*impl(_skiplist));
	auto it = accessor.find({(uint64_t)key, 0});
	item = (it != accessor.end()) ? (itemid_t *)it->second : nullptr;

	cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
	cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;

	scan_cursor_per_thd[thd_id].count = 0;
	scan_cursor_per_thd[thd_id].pos = 0;
	return RCOK;
}

RC IndexFollySkiplist::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
	itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
	itemid_t * next_item = nullptr;
	if (last_item) {
		next_item = last_item->next;
	}
	if (!samekey && !next_item) {
		FollySkiplistScanCursor &sc = scan_cursor_per_thd[thd_id];

		if (sc.pos >= sc.count) {
			idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
			sc.count = 0;

			Accessor accessor(*impl(_skiplist));
			Skipper skipper(accessor);
			skipper.to({(uint64_t)cur_key + 1, 0});
			while (skipper.good() && sc.count < FOLLY_SKIPLIST_SCAN_BATCH) {
				sc.keys[sc.count] = skipper->first;
				sc.vals[sc.count] = skipper->second;
				sc.count++;
				++skipper;
			}
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
