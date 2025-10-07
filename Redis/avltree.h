#ifndef avltree_H
#define avltree_H

#include <stdint.h>
#include <stdlib.h>

typedef struct AVLNode {
    struct AVLNode *left;
    struct AVLNode *right;
    struct AVLNode *parent;
    uint32_t height; 
    //Number of nodes in the subtree rooted at this node
    uint32_t count;
} AVLNode;

//Initialize a node
void initialise_avltree(AVLNode *node);


//Create a new AVL node
AVLNode *avltree_create_node(void);

//Free an AVL node/tree
void avltree_free_node(AVLNode *node);
void avltree_free_tree(AVLNode *root);
void update_avltree(AVLNode *node);

//Attribute functions
uint32_t avltree_height(const AVLNode *node);
uint32_t avltree_count(const AVLNode *node);

//Operations
AVLNode *avltree_fix(AVLNode *node);
AVLNode *avltree_delete(AVLNode *node);
AVLNode *avltree_rank(AVLNode *current, int64_t target_rank);

#endif