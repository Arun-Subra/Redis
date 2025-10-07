#include <stdlib.h>
#include "hashtable.h"
#include <string.h>

//Initialize a map
void hm_init(struct HMap *m) {
    memset(m, 0, sizeof *m);
}

//Initialize a table (n must be power of two)
static void htab_init(struct HTab *t, size_t n) {
    t->tbl = calloc(n, sizeof *t->tbl);
    t->msk = n - 1;
    t->sz  = 0;
}

//Add node to chain
static void htab_insert(struct HTab *t, struct HNode *n) {
    size_t i = n->hcode & t->msk;
    n->next = t->tbl[i];
    t->tbl[i] = n;
    t->sz++;
}

//Find pointer-to-pointer for matching node
static struct HNode **htab_lookup(struct HTab *t,
                                  struct HNode *key,
                                  bool (*cmp)(struct HNode*, struct HNode*)) {
    if (!t->tbl) return NULL;
    size_t i = key->hcode & t->msk;
    struct HNode **pp = &t->tbl[i];
    while (*pp) {
        if ((*pp)->hcode == key->hcode && cmp(*pp, key))
            return pp;
        pp = &(*pp)->next;
    }
    return NULL;
}

//Detach node from chain
static struct HNode *htab_detach(struct HTab *t, struct HNode **pp) {
    struct HNode *n = *pp;
    *pp = n->next;
    t->sz--;
    return n;
}

//Move some of old table into new one
static void hm_help_rehashing(struct HMap *m) {
    size_t w = 0;
    while (w < REHASH_WORK && m->older.sz) {
        struct HNode **pp = &m->older.tbl[m->migr_pos];
        if (!*pp) {
            m->migr_pos++;
            continue;
        }
        htab_insert(&m->newer, htab_detach(&m->older, pp));
        w++;
    }
    if (!m->older.sz && m->older.tbl) {
        free(m->older.tbl);
        m->older.tbl = NULL;
        m->older.msk = m->older.sz = 0;
    }
}

//Start a resize
static void hm_trigger_rehashing(struct HMap *m) {
    m->older = m->newer;
    htab_init(&m->newer, (m->newer.msk + 1) * 2);
    m->migr_pos = 0;
}

//Wipe all entries
void hm_clear(struct HMap *m) {
    free(m->newer.tbl);
    free(m->older.tbl);
    m->newer.tbl = m->older.tbl = NULL;
    m->newer.msk = m->newer.sz = 0;
    m->older.msk = m->older.sz = 0;
    m->migr_pos = 0;
}

//Return total entries
size_t hm_size(struct HMap *m) {
    return m->newer.sz + m->older.sz;
}

//Lookup a node by key
struct HNode *hm_lookup(struct HMap *m, struct HNode *key,
                        bool (*cmp)(struct HNode*, struct HNode*)) {
    hm_help_rehashing(m);
    struct HNode **pp = htab_lookup(&m->newer, key, cmp);
    if (!pp)
        pp = htab_lookup(&m->older, key, cmp);
    return pp ? *pp : NULL;
}

//Insert a node
void hm_insert(struct HMap *m, struct HNode *n) {
    if (!m->newer.tbl)
        htab_init(&m->newer, 4);
    htab_insert(&m->newer, n);
    if (!m->older.tbl && m->newer.sz >= (m->newer.msk + 1) * MAX_LOAD)
        hm_trigger_rehashing(m);
    hm_help_rehashing(m);
}

//Remove and return a node
struct HNode *hm_delete(struct HMap *m, struct HNode *key,
                        bool (*cmp)(struct HNode*, struct HNode*)) {
    hm_help_rehashing(m);
    struct HNode **pp = htab_lookup(&m->newer, key, cmp);
    if (pp)
        return htab_detach(&m->newer, pp);
    pp = htab_lookup(&m->older, key, cmp);
    return pp ? htab_detach(&m->older, pp) : NULL;
}
