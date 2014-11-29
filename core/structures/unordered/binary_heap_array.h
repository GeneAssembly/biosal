
#ifndef CORE_BINARY_HEAP_ARRAY_H
#define CORE_BINARY_HEAP_ARRAY_H

#include <core/structures/vector.h>

/**
 * @see http://www.stack.nl/~dimitri/doxygen/manual/docblocks.html
 */

/**
 * Binary heap.
 *
 * This is either a min-heap or a max-heap.
 *
 * The implementation is based on the Wikipedia page.
 *
 * @see http://en.wikipedia.org/wiki/Binary_heap
 */
struct core_binary_heap_array {
    struct core_vector vector;
    int key_size;
    int value_size;
    int size;
    int (*test_relation)(struct core_binary_heap_array *self, void *key1, void *key2);
};

struct core_memory_pool;

/**
 * @param flags setup flags, can include CORE_BINARY_HEAP_MIN or CORE_BINARY_HEAP_MAX.
 */
void core_binary_heap_array_init(struct core_binary_heap_array *self, int key_size,
                int value_size, uint32_t flags);
void core_binary_heap_array_destroy(struct core_binary_heap_array *self);

/**
 * Insert a key-value pair.
 *
 * @return true is inserted
 */
int core_binary_heap_array_insert(struct core_binary_heap_array *self, void *key, void *value);

/**
 *
 * @return true if there is a root.
 */
int core_binary_heap_array_get_root(struct core_binary_heap_array *self, void **key, void **value);

/**
 * Delete the root.
 */
int core_binary_heap_array_delete_root(struct core_binary_heap_array *self);

/**
 * @return the size of the heap (the number of key-value pairs).
 */
int core_binary_heap_array_size(struct core_binary_heap_array *self);

/**
 * @return true if empty, false otherwise.
 */
int core_binary_heap_array_empty(struct core_binary_heap_array *self);

void core_binary_heap_array_set_memory_pool(struct core_binary_heap_array *self,
                struct core_memory_pool *pool);

#endif
