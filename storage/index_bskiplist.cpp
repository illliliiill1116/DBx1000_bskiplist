#include "index_bskiplist.h"
#include "mem_alloc.h"
#include "global.h"


struct BatchFillArg {
    bsl_key_t *keys;
    bsl_val_t *vals;
    int        count;
    int        capacity;
};

static void bsl_scan_batch_callback(bsl_range_t range, void *arg) {
    BatchFillArg *a = (BatchFillArg *)arg;
    for (size_t i = 0; i < range.count && a->count < a->capacity; ++i) {
        a->keys[a->count] = range.keys[i];
        a->vals[a->count] = range.vals[i];
        a->count++;
    }
}

RC IndexBskiplist::init(int part_cnt) {
    _bsl_list = bsl_new();
    M_ASSERT(_bsl_list != nullptr, "Failed to create bskiplist!");

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

RC IndexBskiplist::init(int part_cnt, table_t * table) {
    this->table = table;
    return init(part_cnt);
}

// NOTE: bskiplist reserves key 0 (BSL_KEY_MIN) and UINT64_MAX (BSL_KEY_MAX)
// as internal sentinels, so every real key is stored/looked-up as (key + 1).
bool IndexBskiplist::index_exist(idx_key_t key) {
    return bsl_get(_bsl_list, (bsl_key_t)key + 1, nullptr) == 1;
}

RC IndexBskiplist::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
    item->next = nullptr;
    int res = bsl_insert(_bsl_list, (bsl_key_t)key + 1, (bsl_val_t)item);
    M_ASSERT(res == 1, "Failed to insert into bskiplist!");
    return RCOK;
}

RC IndexBskiplist::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
    bsl_val_t val = 0;
    int res = bsl_get(_bsl_list, (bsl_key_t)key + 1, &val);
    M_ASSERT(res == 1, "Key does not exist!");
    item = (itemid_t *)val;
    cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
    cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;

    scan_cursor_per_thd[thd_id].count = 0;
    scan_cursor_per_thd[thd_id].pos = 0;
    return RCOK;
}

RC IndexBskiplist::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
    itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
    itemid_t * next_item = nullptr;
    if (last_item) {
        next_item = last_item->next;
    }
    if (!samekey && !next_item) {
        BskiplistScanCursor &sc = scan_cursor_per_thd[thd_id];

        if (sc.pos >= sc.count) {
            idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
            BatchFillArg arg = { sc.keys, sc.vals, 0, BSKIPLIST_SCAN_BATCH };
            bsl_scan_n_batch(_bsl_list, (bsl_key_t)(cur_key + 1) + 1,
                              BSKIPLIST_SCAN_BATCH, bsl_scan_batch_callback, &arg);
            sc.count = arg.count;
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