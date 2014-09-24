
#ifndef BSAL_RED_BLACK_TREE_H
#define BSAL_RED_BLACK_TREE_H

struct bsal_red_black_node;
struct bsal_memory_pool;

/*
 * A red-black tree.
 *
 * \see http://en.wikipedia.org/wiki/Red%E2%80%93black_tree
 *
 * The 5 rules are:
 *
 * 1. A node is red or black.
 * 2. The root is black.
 * 3. All leaf nodes are black.
 * 4. Any red node has 2 black child nodes.
 * 5. Every path from given node to any of its descendent leaf node contains
 *    the same number of black nodes.
 */
struct bsal_red_black_tree {
    struct bsal_red_black_node *root;
    struct bsal_memory_pool *memory_pool;
    int size;
};

void bsal_red_black_tree_init(struct bsal_red_black_tree *self);
void bsal_red_black_tree_destroy(struct bsal_red_black_tree *self);

void bsal_red_black_tree_add(struct bsal_red_black_tree *self, int key);
void bsal_red_black_tree_delete(struct bsal_red_black_tree *self, int key);

/*
 * Check the 5 red-black tree rules.
 *
 * If there is a problem, a non-zero value is returned.
 */
int bsal_red_black_tree_has_ignored_rules(struct bsal_red_black_tree *self);
void bsal_red_black_tree_set_memory_pool(struct bsal_red_black_tree *self,
                struct bsal_memory_pool *memory_pool);

int bsal_red_black_tree_size(struct bsal_red_black_tree *self);

void bsal_red_black_tree_free_node(struct bsal_red_black_tree *self,
                struct bsal_red_black_node *node);

#endif
