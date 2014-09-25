
#include <core/structures/ordered/red_black_tree.h>

#include <core/system/memory_pool.h>

#include "test.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    BEGIN_TESTS();

    int i;
    int count;
    int key;
    struct bsal_red_black_tree tree;
    struct bsal_memory_pool memory_pool;

    bsal_memory_pool_init(&memory_pool, 1024*1024, -1);

    count = 100000;

    srand(88);

    bsal_red_black_tree_init(&tree, sizeof(int), sizeof(int));
    bsal_red_black_tree_set_memory_pool(&tree, &memory_pool);

#if 0
    bsal_red_black_tree_print(&tree);
#endif

    for (i = 0; i < count; ++i) {
        key = rand() % 300;

#if 0
        printf("add node %d (tree before)\n", key);
        bsal_red_black_tree_print(&tree);
#endif
        TEST_INT_EQUALS(bsal_red_black_tree_has_ignored_rules(&tree), 0);
        bsal_red_black_tree_add(&tree, &key);
        TEST_INT_EQUALS(bsal_red_black_tree_has_ignored_rules(&tree), 0);
        TEST_INT_EQUALS(bsal_red_black_tree_size(&tree), i + 1);

        TEST_POINTER_NOT_EQUALS(bsal_red_black_tree_get(&tree, &key), NULL);
#if 0
        bsal_red_black_tree_print(&tree);
#endif
    }

#if 0
    bsal_red_black_tree_print(&tree);
#endif
#if 0
    bsal_memory_pool_examine(&memory_pool);
#endif

    bsal_red_black_tree_destroy(&tree);

    TEST_INT_EQUALS(bsal_memory_pool_profile_balance_count(&memory_pool), 0);

    bsal_memory_pool_destroy(&memory_pool);

    END_TESTS();

    return 0;
}
