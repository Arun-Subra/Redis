#ifndef HMAP_H
#define HMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define REHASH_WORK 128
#define MAX_LOAD    8

//Node for chaining
struct HNode {
    struct HNode *next;
    uint64_t hcode;
};

//Chaining table
struct HTab {
    struct HNode **tbl;
    size_t msk;
    size_t sz;
};

//Map with progressive rehash
struct HMap {
    struct HTab newer;
    struct HTab older;
    size_t migr_pos;
};

//Initialize a map
void hm_init(struct HMap *m);

//Clear all entries in map
void   hm_clear(struct HMap *m);

//Return total number of entries
size_t hm_size(struct HMap *m);

//Lookup a node by key
struct HNode *hm_lookup(struct HMap *m, struct HNode *key,
                        bool (*cmp)(struct HNode*, struct HNode*));

//Insert a node           
void   hm_insert(struct HMap *m, struct HNode *n);

//Delete a node and return it
struct HNode *hm_delete(struct HMap *m, struct HNode *key,
                        bool (*cmp)(struct HNode*, struct HNode*));

#endif
