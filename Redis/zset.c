#include "zset.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//Initialise the ZSet by setting the root to NULL and initialising the hash map
void zset_init(ZSet *zs) {
    zs->root = NULL;
    hm_init(&zs->map);
}

//Simple hash function (SDBM variant) to generate a 32-bit hash from a string
uint32_t hash(const void *data, size_t len) {
    const unsigned char *str = data;
    unsigned long hash = 0;
    for (size_t i = 0; i < len; ++i) {
        hash = str[i] + (hash << 6) + (hash << 16) - hash;
    }
    return (uint32_t)hash;
}



//Allocate and initialise a new ZNode with the given name and score
ZNode *create_znode(char *name, size_t keylen, double score) {
    ZNode *zn = (ZNode *)malloc(sizeof(ZNode) + keylen + 1);
    if (!zn) return NULL;

    initialise_avltree(&zn->tree);
    zn->tree.height = 1;
    zn->tree.count = 1;

    zn->score = score;
    zn->keylen = keylen;
    zn->hnode.next = NULL;
    zn->hnode.hcode = hash(name, keylen);
    memcpy(zn->key, name, keylen);
    zn->key[keylen] = '\0';
    return zn;
}

//Compare two HNode keys by their lengths and contents
bool compare(struct HNode *a, struct HNode *b) {
    ZNode *za = container_of(a, ZNode, hnode);
    ZNode *zb = container_of(b, ZNode, hnode);

    if (za->keylen != zb->keylen) return false;
    return memcmp(za->key, zb->key, za->keylen) == 0;
}

//Lookup a ZNode in the hash map by key
ZNode *zset_lookup(ZSet *zs, const char *key, size_t keylen) {
    // Allocate correctly
    size_t size = sizeof(ZNode) + keylen;
    ZNode *probe = malloc(size);
    if (!probe) return NULL;

    probe->keylen = keylen;
    memcpy(probe->key, key, keylen);
    probe->hnode.hcode = hash(key, keylen);

    struct HNode *found = hm_lookup(&zs->map, &probe->hnode, compare);

    // Save the return value before freeing
    ZNode *result = found ? container_of(found, ZNode, hnode) : NULL;

    free(probe);  // Safe to free after lookup

    return result;
}




//Comparison function for AVL tree insertion based on score, then key
static bool znode_less(AVLNode *a, AVLNode *b) {
    ZNode *za = container_of(a, ZNode, tree);
    ZNode *zb = container_of(b, ZNode, tree);

    if (za->score < zb->score) return true;
    if (za->score > zb->score) return false;

    int cmp = memcmp(za->key, zb->key, za->keylen < zb->keylen ? za->keylen : zb->keylen);
    if (cmp < 0) return true;
    if (cmp > 0) return false;
    return za->keylen < zb->keylen;
}

//Insert a ZNode into the AVL tree while maintaining balance
void avltree_insert(ZSet *zset, ZNode *node) {
    AVLNode **current = &zset->root;
    AVLNode *parent = NULL;

    while (*current) {
        parent = *current;
        if (znode_less(&node->tree, *current)) {
            current = &(*current)->left;
        } else {
            current = &(*current)->right;
        }
    }

    *current = &node->tree;
    node->tree.parent = parent;

    AVLNode *to_fix = &node->tree;
    while (to_fix) {
        update_avltree(to_fix);
        to_fix = to_fix->parent;
    }

    zset->root = avltree_fix(&node->tree);
}

//Insert a key-score pair into the ZSet
bool zset_insert(ZSet *zs, const char *key, size_t keylen, double score) {
    ZNode *zn = zset_lookup(zs, key, keylen);
    if (zn) {
        zset_delete(zs, zn);
    }
    zn = create_znode((char *)key, keylen, score);
    if (!zn) {
        return false;
    }
    hm_insert(&zs->map, &zn->hnode);
    avltree_insert(zs, zn);
    return true;
}

//Remove a ZNode from both the hash map and AVL tree
void zset_delete(ZSet *zs, ZNode *znode) {
    if (!zs || !znode) return;

    // Remove from AVL tree
    AVLNode *avlnode = &znode->tree;
    zs->root = avltree_delete(avlnode);

    // Remove from hash map using the ZNode's hash node
    hm_delete(&zs->map, &znode->hnode, compare);

    // Free the node memory
    free(znode);
}




//Seek the smallest element >= (score, key) in the AVL tree
ZNode *zset_seekge(ZSet *zs, double score, const char *key, size_t keylen) {
    AVLNode *cur = zs->root;
    ZNode *candidate = NULL;

    struct {
        AVLNode tree;
        double score;
        size_t keylen;
        char key[256];
    } temp;

    temp.score = score;
    temp.keylen = keylen;
    if (keylen > sizeof(temp.key)) {
        return NULL;
    }
    memcpy(temp.key, key, keylen);

    while (cur) {
        ZNode *zcur = container_of(cur, ZNode, tree);

        if (!znode_less(&zcur->tree, &temp.tree)) {
            // zcur >= temp
            candidate = zcur;
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }

    return candidate;
}

//Free all resources associated with a ZSet
void zset_clear(ZSet *zs) {
    hm_clear(&zs->map);
    avltree_free_tree(zs->root);
    zs->root = NULL;
}


//Return the node at a given rank from the AVL tree
ZNode *znode_rank(ZNode *zn, int64_t rank) {
    AVLNode *current = avltree_rank(&zn->tree, rank);
    if (!current) {
        return NULL;
    }
    return container_of(current, ZNode, tree);
}

//In-order traversal of AVL tree to collect nodes in sorted order
static void in_order_traversal(AVLNode *node, ZNode **nodes, size_t *index, size_t size) {
    if (!node || *index >= size) {
        return;
    }
    in_order_traversal(node->left, nodes, index, size);
    if (*index < size) {
        nodes[*index] = container_of(node, ZNode, tree);
        (*index)++;
    }
    in_order_traversal(node->right, nodes, index, size);
}

//Return all nodes in sorted order (score then key)
ZNode** zset_all_nodes(ZSet *zset, size_t *count) {
    if (!zset || !count) {
        return NULL;
    }

    *count = avltree_count(zset->root);
    printf("avltree_count(root) = %zu\n", *count);
    if (*count == 0) {
        return NULL;
    }

    ZNode **nodes = malloc(sizeof(ZNode*) * (*count));
    if (!nodes) {
        return NULL;
    }

    size_t index = 0;
    in_order_traversal(zset->root, nodes, &index, *count);

    return nodes;
}

//Return an array of nodes between the given rank range (inclusive)
ZNode** zset_range(ZSet* zset, int64_t start, int64_t end, size_t* count) {
    if (!zset || !count) {
        return NULL;
    }

    size_t total_count;
    ZNode **all_nodes = zset_all_nodes(zset, &total_count);
    if (!all_nodes) {
        *count = 0;
        return NULL;
    }

    if (start < 0) start = total_count + start;
    if (end < 0) end = total_count + end;
    
    if (start < 0) start = 0;
    if (end >= (int64_t)total_count) end = total_count - 1;
    
    if (start > end) {
        free(all_nodes);
        *count = 0;
        return NULL;
    }

    size_t range_size = end - start + 1;
    ZNode **results = malloc(sizeof(ZNode*) * range_size);
    if (!results) {
        free(all_nodes);
        *count = 0;
        return NULL;
    }

    memcpy(results, &all_nodes[start], range_size * sizeof(ZNode*));
    free(all_nodes);
    *count = range_size;
    return results;
}