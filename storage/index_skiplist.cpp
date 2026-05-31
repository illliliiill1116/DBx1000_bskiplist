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
#include "index_skiplist.h"
#include "mem_alloc.h"
#include "global.h"

#define MAXKEY UINT64_MAX
extern myrand threadRNGs[PREFETCH_SIZE_WORDS * MAX_PARALLELISM];

unsigned int IndexSkiplist::sl_randomLevel(const int thd_id) { // TODO: understand how to utilize this, add credit in file description
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

sl_node_t * IndexSkiplist::allocate_node(unsigned int toplevel, idx_key_t key, int part_id) {
    sl_node_t * new_node = (sl_node_t *) mem_allocator.alloc(sizeof(sl_node_t) + toplevel*sizeof(sl_node_t *), part_id);
    new_node->toplevel = toplevel;
    new_node->key = key;
    new_node->items = nullptr;
    return new_node;
}

sl_node_t * IndexSkiplist::weak_search_predecessors(idx_key_t key, sl_node_t **pa, sl_node_t **na){
    sl_node_t *x, *x_next;
    x = head;
    x_next = head->next[MAXLEVEL-1];
    for (int i = (pa) ? MAXLEVEL-1 : toplevel; i >= 0; i--) { // if pa is null, start from highest used level
        x_next = x->next[i];
        assert(x_next != nullptr);
        while (key > x_next->key) {
            x = x_next;
            x_next = x->next[i];
            assert(x_next != nullptr);
        }
        if ( pa ) pa[i] = x;
        if ( na ) na[i] = x_next;
    }
    return x_next;
}

RC IndexSkiplist::init(int part_cnt) {
    head = allocate_node(MAXLEVEL-1, 0);
    auto last = allocate_node(MAXLEVEL-1, MAXKEY);
    for (int i = 0; i <= MAXLEVEL-1; i++) {
        last->next[i] = nullptr;
        head->next[i] = last;
    }
    toplevel = 0;

    return RCOK;
}

RC IndexSkiplist::init(int part_cnt, table_t * table) {
    this->table = table;
	init(part_cnt);
	return RCOK;
}

bool IndexSkiplist::index_exist(idx_key_t key) {
    auto node = weak_search_predecessors(key, nullptr, nullptr);
    return (node->key == key);
}

RC IndexSkiplist::index_insert(idx_key_t key, itemid_t * item, int part_id, uint64_t thd_id) {
    sl_node_t * preds[MAXLEVEL];
    sl_node_t * succs[MAXLEVEL];
    sl_node_t *pred, *succ, *new_next, *new_node = nullptr;
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
    item->next = nullptr;
    new_node->items = item;

    level = new_node->toplevel;
    }

    for (i = 0; i <= level; i++)
    {
        new_node->next[i] = succs[i];
    }
    
    if (!ATOM_CAS(preds[0]->next[0], succ, new_node)) { // the CAS includes a memory barrier
        goto retry;
    }

    i = 1;
    while (i<=level) {
        pred = preds[i];
        succ = succs[i];

        new_next = new_node->next[i];
        /* Ensure forward pointer of new node is up to date. */
        if (new_next != succ) {
            ATOM_CAS(new_node->next[i], new_next, succ);
        }

        /* Ensure we have unique key values at every level. */
        if ( succ->key == key ) goto new_world_view;
        assert((pred == head || pred->key < key) && (succ->key > key));

        /* Replumb predecessor's forward pointer. */
        if ( !ATOM_CAS(pred->next[i], succ, new_node) )
        {
        new_world_view:
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

RC IndexSkiplist::index_read(idx_key_t key, itemid_t * &item, int part_id, int thd_id) {
    auto node = weak_search_predecessors(key, nullptr, nullptr);
    M_ASSERT(node->key == key, "Key does not exist!");
	item = node->items;
    cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS] = node;
    cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS] = node->items;
    return RCOK;
}

RC IndexSkiplist::index_next(uint64_t thd_id, itemid_t * &item, bool samekey) { // must be called after a read was performed
    itemid_t * last_item = cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS];
    itemid_t * next_item = nullptr;
    if (last_item) next_item = last_item->next;
    if (!samekey && !next_item) {
        auto last_node = cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS];
        if (last_node) {
            auto next_node = last_node->next[0];
            cur_node_per_thd[thd_id*PREFETCH_SIZE_WORDS] = next_node;
            if (next_node) next_item = next_node->items;
        }
    }
    cur_item_per_thd[thd_id*PREFETCH_SIZE_WORDS] = next_item;
    item = next_item;
    return RCOK;
}