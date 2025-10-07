#include <stdbool.h>

#include "avltree.h"

//Initialise a newly allocated AVL node with defauly values
void initialise_avltree(AVLNode *node) {
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    node->height = 1;
    node->count = 1;
}

//Allocate and return a new AVL node
AVLNode *avltree_create_node(void) {
    AVLNode *node = (AVLNode *)malloc(sizeof(AVLNode));
    if (!node) {
        return NULL;
    }
    initialise_avltree(node);
    return node;
}


//Free a single AVL node
void avltree_free_node(AVLNode *node) {
    if (node) {
        free(node);
    }
}

//Return the height of the node (0 if NULL)
uint32_t avltree_height(const AVLNode *node) {
    return node ? node->height : 0;
}

//Return the subtree size rooted at node (0 if NULL)
uint32_t avltree_count(const AVLNode *node) {
    return node ? node->count : 0;
}

//Recalculate the height and count of a node based on its children
void update_avltree(AVLNode *node) {
    int lh = node->left ? node->left->height : 0;
    int rh = node->right ? node->right->height : 0;
    node->height = (lh > rh ? lh : rh) + 1;

    int lc = node->left ? node->left->count : 0;
    int rc = node->right ? node->right->count : 0;
    node->count = lc + rc + 1;
}


//Perform a left rotation on the given node
static AVLNode* rotate_left(AVLNode *node) {
    AVLNode *new_root = node->right;

    // Rotate
    node->right = new_root->left;
    if (new_root->left) new_root->left->parent = node;

    new_root->left = node;
    new_root->parent = node->parent;
    node->parent = new_root;

    // Update nodes
    update_avltree(node);
    update_avltree(new_root);
    return new_root;
}

//Perform a right rotation on the given node
static AVLNode* rotate_right(AVLNode *node) {
    AVLNode *new_root = node->left;

    // Rotate
    node->left = new_root->right;
    if (new_root->right) new_root->right->parent = node;

    new_root->right = node;
    new_root->parent = node->parent;
    node->parent = new_root;

    // Update nodes
    update_avltree(node);
    update_avltree(new_root);
    return new_root;
}

//Balance a node that is left-heavy
static AVLNode *fix_left(AVLNode *node) {
    if (avltree_height(node->left->left) < avltree_height(node->left->right)) {
        // Right
        node->left = rotate_left(node->left);
    }
    // Left
    return rotate_right(node);
}

//Balance a node that is right heavy
static AVLNode *fix_right(AVLNode *node) {
    if (avltree_height(node->right->right) < avltree_height(node->right->left)) {
        // Left
        node->right = rotate_right(node->right);
    }
    // Right
    return rotate_left(node);
}

//Walk upward from the given node, balancing the tree
//Returns the new root after rebalancing
AVLNode *avltree_fix(AVLNode *node) {
    if (node == NULL) return NULL;

    while (1) {
        update_avltree(node);

        uint32_t left_height = avltree_height(node->left);
        uint32_t right_height = avltree_height(node->right);

        AVLNode **parent_link = NULL;
        if (node->parent) {
            parent_link = (node->parent->left == node) 
                          ? &node->parent->left 
                          : &node->parent->right;
        }

        if (left_height > right_height + 1) {
            node = fix_left(node);
        } else if (right_height > left_height + 1) {
            node = fix_right(node);
        }

        if (!parent_link) {
            //Node is now the root after balancing
            node->parent = NULL;
            return node;
        }

        *parent_link = node;
        node = node->parent;
    }
}

//Delete a node with 0 or 1 child
//Returns the new subtree root after deletion and rebalancing
static AVLNode *delete_trivial(AVLNode *target) {
    AVLNode *replacement = target->left ? target->left : target->right;
    AVLNode *parent_node = target->parent;

    if (replacement) {
        replacement->parent = parent_node;
    }

    if (!parent_node) {
        return replacement;
    }

    AVLNode **link_to_target = (parent_node->left == target)
                              ? &parent_node->left
                              : &parent_node->right;
    *link_to_target = replacement;

    return avltree_fix(parent_node);
}

//Delete a node with two children by replacing with in-order successor
//Returns the new root of the subtree after deletion
AVLNode *avltree_delete(AVLNode *node) {
    // Handle cases where node has at most one child
    if (!(node->left && node->right)) {
        return delete_trivial(node);
    }

    // Locate the successor: leftmost node in right subtree
    AVLNode *successor = node->right;
    while (successor->left) {
        successor = successor->left;
    }

    // Remove the successor from the tree
    AVLNode *new_root = delete_trivial(successor);

    // Manually transfer AVL fields from node to successor
    successor->left   = node->left;
    successor->right  = node->right;
    successor->parent = node->parent;
    successor->height = node->height;
    successor->count  = node->count;

    if (successor->left)  successor->left->parent  = successor;
    if (successor->right) successor->right->parent = successor;

    // Link the successor in place of the node being deleted
    if (!node->parent) {
        new_root = successor;
    } else {
        if (node->parent->left == node) {
            node->parent->left = successor;
        } else {
            node->parent->right = successor;
        }
    }

    // DO NOT free node here — it is freed externally (e.g., in zset_delete)
    return new_root;
}


//Return the node at a given in-order rank (0-based index)
//Returns NULL if out of bounds or not found
AVLNode *avltree_rank(AVLNode *current, int64_t target_rank) {
    int64_t current_pos = 0;
    
    while (current_pos != target_rank) {
        int64_t right_count = avltree_count(current->right);
        int64_t left_count = avltree_count(current->left);
        
        if (current_pos < target_rank && current_pos + right_count >= target_rank) {
            // Move to the right subtree and update position
            current = current->right;
            current_pos += left_count + 1;
        } else if (current_pos > target_rank && current_pos - left_count <= target_rank) {
            // Move to the left subtree and update position
            current = current->left;
            current_pos -= right_count + 1;
        } else {
            // Move up to parent and adjust position accordingly
            AVLNode *parent = current->parent;
            if (!parent) return NULL;
            
            if (parent->right == current) {
                current_pos -= left_count + 1;
            } else {
                current_pos += right_count + 1;
            }
            current = parent;
        }
    }
    return current;
}

//Recursively free all nodes in a subtree
void avltree_free_tree(AVLNode *root) {
    if (!root) return;
    avltree_free_tree(root->left);
    avltree_free_tree(root->right);
    avltree_free_node(root);
}