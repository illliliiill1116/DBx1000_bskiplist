#include "index_bskiplist.h"
#include "mem_alloc.h"
#include "global.h"

struct ScanResult {
    bsl_key_t key;
    bsl_val_t val;
    bool found;
};

static void bsl_scan_callback(bsl_key_t k, bsl_val_t v, void *arg) {
    ScanResult *res = (ScanResult *)arg;
    if (!res->found) {
        res->key = k;
        res->val = v;
        res->found = true;
    }
}

RC IndexBskiplist::init(int part_cnt) {
    _bsl_list = bsl_new();
    M_ASSERT(_bsl_list != nullptr, "Failed to create bskiplist!");
    
    for (int i = 0; i < PREFETCH_SIZE_WORDS * THREAD_CNT; ++i) {
        cur_key_per_thd[i] = 0;
        cur_item_per_thd[i] = nullptr;
    }
    return RCOK;
}

RC IndexBskiplist::init(int part_cnt, table_t * table) {
    this->table = table;
    return init(part_cnt);
}

bool IndexBskiplist::index_exist(idx_key_t key) {
    return bsl_get(_bsl_list, (bsl_key_t)key, nullptr) == 1;
}

RC IndexBskiplist::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
    item->next = nullptr;
    int res = bsl_insert(_bsl_list, (bsl_key_t)key, (bsl_val_t)item);
    M_ASSERT(res == 1, "Failed to insert into bskiplist!");
    return RCOK;
}

RC IndexBskiplist::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
    bsl_val_t val = 0;
    int res = bsl_get(_bsl_list, (bsl_key_t)key, &val);
    M_ASSERT(res == 1, "Key does not exist!");
    item = (itemid_t *)val;
    cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = key;
    cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = item;
    return RCOK;
}

RC IndexBskiplist::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) {
    itemid_t * last_item = cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS];
    itemid_t * next_item = nullptr;
    if (last_item) {
        next_item = last_item->next;
    }
    if (!samekey && !next_item) {
        ScanResult result = {0, 0, false};
        idx_key_t cur_key = cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS];
        
        bsl_scan_n(_bsl_list, (bsl_key_t)(cur_key + 1), 1, bsl_scan_callback, &result);
        
        if (result.found) {
            cur_key_per_thd[thd_id * PREFETCH_SIZE_WORDS] = result.key;
            next_item = (itemid_t *)result.val;
        } else {
            next_item = nullptr;
        }
    }
    cur_item_per_thd[thd_id * PREFETCH_SIZE_WORDS] = next_item;
    item = next_item;
    return RCOK;
}
