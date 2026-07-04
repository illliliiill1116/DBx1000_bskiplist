#include "index_rw_bskiplist.h"
#include "mem_alloc.h"
#include "global.h"

#include "RW_bskiplist/include/BSkipList.hpp"


using bskip_traits_t = BSkip_traits<true, 128ul, 64ul, uint64_t, uint64_t>;
using bskip_t = BSkip<bskip_traits_t>;

static inline bskip_t * impl(void * p) { return (bskip_t *)p; }

RC IndexRWBskiplist::init(int part_cnt) {
    _bskip = new bskip_t();
    M_ASSERT(_bskip != nullptr, "Failed to create bskip list!");

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

RC IndexRWBskiplist::init(int part_cnt, table_t * table) {
    this->table = table;
    return init(part_cnt);
}

IndexRWBskiplist::~IndexRWBskiplist() {
    delete impl(_bskip);
}

// NOTE: bskiplist reserves key 0 (BSL_KEY_MIN) and UINT64_MAX (BSL_KEY_MAX)
// as internal sentinels, so every real key is stored/looked-up as (key + 1).
bool IndexRWBskiplist::index_exist(idx_key_t key) {
    return impl(_bskip)->exists((uint64_t)key + 1);
}

RC IndexRWBskiplist::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
    item->next = nullptr;
    bool res = impl(_bskip)->insert({(uint64_t)key + 1, (uint64_t)item});
    M_ASSERT(res, "Failed to insert into rw-bskiplist!");
    return RCOK;
}

RC IndexRWBskiplist::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
    //M_ASSERT(impl(_bskip)->exists((uint64_t)key + 1), "Key does not exist!");
    /*
    if (!impl(_bskip)->exists((uint64_t)key + 1))
    {
        printf("key: %ld\n", key+1);
        impl(_bskip)->validate_structure();
    }
    */
    auto val = impl(_bskip)->value((uint64_t)key + 1);
    item = (itemid_t *)std::get<0>(val);

    cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
    cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;

    scan_cursor_per_thd[thd_id].count = 0;
    scan_cursor_per_thd[thd_id].pos = 0;
    return RCOK;
}

RC IndexRWBskiplist::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
    itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
    itemid_t * next_item = nullptr;
    if (last_item) {
        next_item = last_item->next;
    }
    if (!samekey && !next_item) {
        BskipScanCursor &sc = scan_cursor_per_thd[thd_id];

        if (sc.pos >= sc.count) {
            idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
            sc.count = 0;
            impl(_bskip)->map_range_length((uint64_t)(cur_key + 1) + 1, BSKIP_SCAN_BATCH,
                [&sc](auto k, auto v) {
                    if (sc.count < BSKIP_SCAN_BATCH) {
                        sc.keys[sc.count] = k;
                        sc.vals[sc.count] = std::get<0>(v);
                        sc.count++;
                    }
                });
            sc.pos = 0;
        }

        if (sc.pos < sc.count) {
            next_item = (itemid_t *)sc.vals[sc.pos];
            // convert the internal (+1) key back to the caller's key space
            cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = (idx_key_t)(sc.keys[sc.pos] - 1);
            sc.pos++;
        } else {
            next_item = nullptr;
        }
    }
    cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = next_item;
    item = next_item;
    return RCOK;
}
