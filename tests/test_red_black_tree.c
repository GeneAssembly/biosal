
#include <core/structures/ordered/red_black_tree.h>

#include <core/structures/vector.h>

#include <core/system/memory_pool.h>

#include "test.h"

#include <stdlib.h>

/*
*/
#define TEST_DELETE

int main(int argc, char **argv)
{
    BEGIN_TESTS();

    int i;
    int lowest;
    int count;
    int key;
    struct bsal_red_black_tree tree;
    struct bsal_memory_pool memory_pool;
    struct bsal_vector keys;
    int *result;
    int size;

    lowest = 0;
    bsal_memory_pool_init(&memory_pool, 1024*1024, -1);

    bsal_vector_init(&keys, sizeof(int));

#if 0
#endif
    count = 100000;
#if 0
    count = 1;
#endif

    srand(88);

    bsal_red_black_tree_init(&tree, sizeof(int), sizeof(int), &memory_pool);

#if 0
    bsal_red_black_tree_set_memory_pool(&tree, &memory_pool);
#endif

#if 0
    bsal_red_black_tree_print(&tree);
#endif

    for (i = 0; i < count; ++i) {
        key = rand() % (2 * count);

        bsal_vector_push_back(&keys, &key);

        if (i == 0)
            lowest = key;

        if (key < lowest)
            lowest = key;

#if 0
        printf("add node %d (tree before)\n", key);
        bsal_red_black_tree_print(&tree);
#endif
        TEST_INT_EQUALS(bsal_red_black_tree_has_ignored_rules(&tree), 0);
        bsal_red_black_tree_add(&tree, &key);
        TEST_INT_EQUALS(bsal_red_black_tree_has_ignored_rules(&tree), 0);
        TEST_INT_EQUALS(bsal_red_black_tree_size(&tree), i + 1);

        TEST_POINTER_NOT_EQUALS(bsal_red_black_tree_get(&tree, &key), NULL);

        result = bsal_red_black_tree_get_lowest_key(&tree);
        TEST_POINTER_NOT_EQUALS(result, NULL);

#if 0
        bsal_red_black_tree_run_assertions(&tree);
#endif

#if 0
        bsal_red_black_tree_print(&tree);
        TEST_INT_EQUALS(*result, lowest);
#endif

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

    bsal_red_black_tree_run_assertions(&tree);

    count = 0;
    size = count;

    for (i = 0; i < count; ++i) {
        key = bsal_vector_at_as_int(&keys, i);

        result = bsal_red_black_tree_get(&tree, &key);

        TEST_POINTER_NOT_EQUALS(result, NULL);

#ifdef TEST_DELETE
        bsal_red_black_tree_delete(&tree, &key);
        --size;
#endif

        TEST_INT_EQUALS(bsal_red_black_tree_size(&tree), size);
    }

    bsal_red_black_tree_destroy(&tree);
    bsal_vector_destroy(&keys);

    TEST_INT_EQUALS(bsal_memory_pool_profile_balance_count(&memory_pool), 0);

    bsal_memory_pool_destroy(&memory_pool);

    /*
     * Small deletions
     */
    {

        struct bsal_red_black_tree tree;
        struct bsal_memory_pool memory_pool;
        struct bsal_vector keys;

        bsal_vector_init(&keys, sizeof(int));

        bsal_memory_pool_init(&memory_pool, 1024*1024, -1);
        bsal_red_black_tree_init(&tree, sizeof(int), sizeof(int), &memory_pool);

        i = 0;
        size = 1;

        while (i < size) {
            key = rand() % (2 * size);

            /*
             * For this test, we don't want duplicates.
             */
            while (bsal_red_black_tree_get(&tree, &key) != NULL) {
                key = rand() % (2 * size);
            }
#if 0
            printf("Add %d\n", key);
#endif
            bsal_red_black_tree_add(&tree, &key);
#if 0
            bsal_red_black_tree_print(&tree);
#endif

            TEST_POINTER_NOT_EQUALS(bsal_red_black_tree_get(&tree, &key), NULL);

            TEST_POINTER_NOT_EQUALS(bsal_red_black_tree_get_lowest_key(&tree), NULL);
            result = bsal_red_black_tree_get(&tree, &key);
            *result = i;
            bsal_vector_push_back(&keys, &key);

            ++i;
        }

        i = 0;

#if 0
        bsal_red_black_tree_print(&tree);
#endif
        while (i < size) {
            key = bsal_vector_at_as_int(&keys, i);
/*
            printf("Looking for %d\n", key);
            */

            TEST_POINTER_NOT_EQUALS(bsal_red_black_tree_get(&tree, &key), NULL);

            bsal_red_black_tree_delete(&tree, &key);
            TEST_POINTER_EQUALS(bsal_red_black_tree_get(&tree, &key), NULL);
            ++i;
        }
        bsal_red_black_tree_destroy(&tree);
        bsal_vector_destroy(&keys);

        TEST_INT_EQUALS(bsal_memory_pool_profile_balance_count(&memory_pool), 0);

        bsal_memory_pool_destroy(&memory_pool);
    }

    END_TESTS();

    return 0;
}
