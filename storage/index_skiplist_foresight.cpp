/*
* This is a variant of Fraser's skiplist algorithm with the following differences:
* As the database does not support deletes, there are no remove operations.
* For that reason, logic referring to deletions (including marked references) is removed
* Besides that, this skiplist implements a multi-set instead of a set, meaning 
* that each node in the skiplist contains a linked list of items with the same key
* instead of a single item. To follow the logic required from an index in DBx1000,
* the skiplist can only be modified by inserting elements (i.e., an existing item cannot
* be subjected to a skiplist 'update' operation, but an additional item with the same key
* may be appended to its node)
*
* The code is largely based on Synchrobenc's implementation code
* Fraser's skiplist is described in: <TODO: add paper link>
* Synchrobench is described in: <TODO: add paper link>
* The RNG is based on the code in Trevor Brown's implementation
*/
#include "index_skiplist_foresight.h"
#include "mem_alloc.h"
#include "global.h"
#include "helper.h"

#define MAXKEY UINT64_MAX
extern myrand threadRNGs[PREFETCH_SIZE_WORDS * MAX_PARALLELISM];

unsigned int IndexSkiplistForesight::sl_randomLevel(const int thd_id) { // TODO: understand how to utilize this, add credit in file description
    unsigned int v = (unsigned int)threadRNGs[thd_id*PREFETCH_SIZE_WORDS].next_unlimited();         // 32-bit word input to count zero bits on right
    unsigned int c = 32;                                                                         // c will be the number of zero bits on the right
    v &= -signed(v);
    if (v) c--;
    if (v & 0x0000FFFF) c -= 16;
    if (v & 0x00FF00FF) c -= 8;
    if (v & 0x0F0F0F0F) c -= 4;
    if (v & 0x33333333) c -= 2;
    if (v & 0x55555555) c -= 1;
    c = c > toplevel+1 ? toplevel+1 : c; // do not raise level by more than 1
    return c > MAXLEVEL-1 ? MAXLEVEL-1 : c; 
}

slf_node_t * IndexSkiplistForesight::allocate_node(unsigned int toplevel, idx_key_t key, int part_id) {
    slf_node_t * new_node = (slf_node_t *) mem_allocator.alloc(sizeof(slf_node_t) + toplevel*sizeof(next_foresight_t), part_id);
    new_node->toplevel = toplevel;
    new_node->key = key;
    new_node->items = nullptr;
    return new_node;
}

slf_node_t * IndexSkiplistForesight::weak_search_predecessors(idx_key_t key, slf_node_t **pa, slf_node_t **na){
    slf_node_t *x, *x_next;
    idx_key_t x_next_k;
    x = head;
    x_next = head->next[MAXLEVEL-1].next_ptr;
#if !FORESIGHT_SIMD
    for (int i = (pa) ? MAXLEVEL-1 : toplevel; i >= 1; i--) { // if pa is null, start from highest used level
        x_next_k = x->next[i].next_key;
        x_next = x->next[i].next_ptr;
        assert(x_next != nullptr);
        while (key > x_next_k) {
            //cout << "thread " << gettid() << " current lvl = " << i << " curr key = " << x->key << " foreseen key = " << x_next_k << " real next = " << x_next->key << std::endl;
            if (x_next->key >= key) break; // avoid reckless advance
            x = x_next;
            x_next_k = x->next[i].next_key;
            x_next = x->next[i].next_ptr;
            assert(x_next != nullptr);
        }
        if ( pa ) pa[i] = x;
        if ( na ) na[i] = x_next;
    }
    // level 0 traversal without foresight
    x_next = x->next[0].next_ptr;
    while (key > x_next->key)
    {
        x = x_next;
        x_next = x->next[0].next_ptr;
        assert(x_next != nullptr);
    }
    if ( pa ) pa[0] = x;
    if ( na ) na[0] = x_next;
#else
 for (int i = (pa) ? MAXLEVEL-1 : toplevel; i >= 0; i--) { // if pa is null, start from highest used level
        read_16_bytes_atomic(&(x->next[i]), (uint64_t *) &x_next, (uint64_t *) &x_next_k);
        assert(x_next != nullptr);
        while (key > x_next_k) {
            x = x_next;
            read_16_bytes_atomic(&(x->next[i]), (uint64_t *) &x_next, (uint64_t *) &x_next_k);
            assert(x_next != nullptr);
        }
        if ( pa ) pa[i] = x;
        if ( na ) na[i] = x_next;
    }
#endif
    return x_next;
}

slf_node_t * IndexSkiplistForesight::weak_search_predecessors_nf(idx_key_t key, slf_node_t **pa, slf_node_t **na){
    slf_node_t *x, *x_next;
    x = head;
    x_next = head->next[MAXLEVEL-1].next_ptr;
    for (int i = (pa) ? MAXLEVEL-1 : toplevel; i >= 0; i--) { // if pa is null, start from highest used level
        x_next = x->next[i].next_ptr;
        assert(x_next != nullptr);
        while (key > x_next->key) {
            x = x_next;
            x_next = x->next[i].next_ptr;
            assert(x_next != nullptr);
        }
        if ( pa ) pa[i] = x;
        if ( na ) na[i] = x_next;
    }
    return x_next;
}

RC IndexSkiplistForesight::init(int part_cnt) {
    head = allocate_node(MAXLEVEL-1, 0);
    auto last = allocate_node(MAXLEVEL-1, MAXKEY);
    for (int i = 0; i <= MAXLEVEL-1; i++) {
        last->next[i].next_ptr = nullptr; // this value should never be used
        last->next[i].next_key = MAXKEY; // this value should never be used
        head->next[i].next_ptr = last;
        head->next[i].next_key = MAXKEY;
    }
    toplevel = 0;

    return RCOK;
}

RC IndexSkiplistForesight::init(int part_cnt, table_t * table) {
    this->table = table;
	init(part_cnt);
	return RCOK;
}

bool IndexSkiplistForesight::index_exist(idx_key_t key) {
    auto node = weak_search_predecessors(key, nullptr, nullptr);
    return (node->key == key);
}

RC IndexSkiplistForesight::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
    slf_node_t * preds[MAXLEVEL];
    slf_node_t * succs[MAXLEVEL];
    slf_node_t *pred, *succ, *new_next, *new_node = nullptr;
    uint64_t i, level;
    
retry:
    succ = weak_search_predecessors(key, preds, succs);
    if (succ->key == key) { // a node with the key already exists, append item to its list
        itemid_t * oldval;
        do {
            oldval = succ->items;
            item->next = oldval;
        }
        while (!ATOM_CAS(succ->items, oldval, item));
        if (new_node) free(new_node); // the new node is not accessible by any other thread and it's safe to free it
        return RCOK;
    }

    if (new_node == nullptr) {
    new_node = allocate_node(sl_randomLevel(thd_id), key, part_id); // TODO: pass thd_id on insert
    new_node->items = item;
    level = new_node->toplevel;
    }

    for (i = 0; i <= level; i++)
    {
        new_node->next[i].next_ptr = succs[i];
        new_node->next[i].next_key = succs[i]->key;
    }
    
    if (!WIDE_CAS(&(preds[0]->next[0]), succ, succ->key, new_node, key)) { // the CAS includes a memory barrier
        goto retry;
    }

    i = 1;
    while (i<=level) {
        pred = preds[i];
        succ = succs[i];

        new_next = new_node->next[i].next_ptr;
        /* Ensure forward pointer of new node is up to date. */
        if (new_next != succ) {
            WIDE_CAS(&(new_node->next[i]), new_next, new_next->key, succ, succ->key);
        }

        /* Ensure we have unique key values at every level. */
        if ( succ->key <= key ) { // also avoid premature descent
            __sync_synchronize(); /* get up-to-date view of the world. */
#if !FORESIGHT_SIMD
            weak_search_predecessors_nf(key, preds, succs); // do not use foresight to avoid another premature descent  
#else
            weak_search_predecessors(key, preds, succs); // premature descent is impossible with SIMD
#endif
            continue;
        }
        assert(((pred == head && key == 0) || pred->key < key) && (succ->key > key));

        /* Replumb predecessor's forward pointer. */
        if ( !WIDE_CAS(&(pred->next[i]), succ, succ->key, new_node, key) )
        {
            __sync_synchronize(); /* get up-to-date view of the world. */
            weak_search_predecessors(key, preds, succs);
            continue;
        }

        /* Succeeded at this level. */
        i++;
    }
    
    auto current_top = toplevel;
    if (level > current_top) {
        ATOM_CAS(toplevel, current_top, level);
    }
    return RCOK;
}

RC IndexSkiplistForesight::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
    auto node = weak_search_predecessors(key, nullptr, nullptr);
    M_ASSERT(node->key == key, "Key does not exist!");
	item = node->items;
    cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS] = node;
    cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS] = node->items;
    return RCOK;
}

RC IndexSkiplistForesight::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) { // must be called after a read was performed
    itemid_t * last_item = cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS];
    itemid_t * next_item = nullptr;
    if (last_item) next_item = last_item->next;
    if (!samekey && !next_item) {
        auto last_node = cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS];
        if (last_node) {
            auto next_node = last_node->next[0].next_ptr;
            cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS] = next_node;
            next_item = next_node->items;
        }
    }
    cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS] = next_item;
    item = next_item;
    return RCOK;
}