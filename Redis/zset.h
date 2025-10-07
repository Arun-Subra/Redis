#ifndef ZSET_H
#define ZSET_H

#include "avltree.h"
#include "hashtable.h"
#include <stdint.h>

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

//Sorted set: avl tree + hashmap
typedef struct {
    AVLNode     *root;   //By (score, key)
    struct HMap  map;    //Lookup by key
} ZSet;

//One element
typedef struct {
    AVLNode      tree;    //Avl node
    struct HNode hnode;   //Hash node
    double       score;   //Element score
    size_t       keylen;  //Length of key
    char         key[];  //Inline key bytes
} ZNode;

void zset_init(ZSet *zs);
uint32_t hash(const void *data, size_t len);
ZNode *create_znode(char *name, size_t keylen, double score);
bool zset_insert(ZSet *zs, const char *key, size_t keylen, double score);
ZNode *zset_lookup(ZSet *zs, const char *key, size_t keylen);
void zset_delete(ZSet *zs, ZNode *zn);
ZNode *zset_seekge(ZSet *zs, double score, const char *key, size_t keylen);
void zset_clear(ZSet *zs);
ZNode *znode_rank(ZNode *zn, int64_t rank);
ZNode** zset_all_nodes(ZSet* zset, size_t* count);
ZNode** zset_range(ZSet* zset, int64_t start, int64_t end, size_t* count);

#endif
