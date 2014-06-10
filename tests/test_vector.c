
#include <structures/vector.h>

#include "test.h"

int main(int argc, char **argv)
{
    BEGIN_TESTS();

    {
        struct bsal_vector vector;
        int i;
        int value;
        int *pointer;
        int expected;
        int actual;
        int j;

        bsal_vector_init(&vector, sizeof(int));
        TEST_INT_EQUALS(bsal_vector_size(&vector), 0);

        for (i = 0; i < 1000; i++) {
            TEST_POINTER_EQUALS(bsal_vector_at(&vector, i), NULL);
            bsal_vector_push_back(&vector, &i);
            TEST_INT_EQUALS(bsal_vector_size(&vector), i + 1);

            value = *(int *)bsal_vector_at(&vector, i);

            TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, i), NULL);
            TEST_INT_EQUALS(value, i);

            pointer = (int *)bsal_vector_at(&vector, i);
            expected = i * i * i;
            *pointer = expected;

            value = *(int *)bsal_vector_at(&vector, i);

            /* restore the value */
            *pointer = i;

            TEST_INT_EQUALS(value, expected);

            for (j = 0; j <= i; j++) {
                expected = j;
                pointer = bsal_vector_at(&vector, j);
                actual = *pointer;
/*
                printf("DEBUG index %d actual %d expected %d bucket %p\n",
                                j, actual, expected, (void *)pointer);
                                */

                TEST_INT_EQUALS(actual, expected);
            }
        }

        bsal_vector_resize(&vector, 42);
        TEST_INT_EQUALS(bsal_vector_size(&vector), 42);

        TEST_POINTER_EQUALS(bsal_vector_at(&vector, 42), NULL);
        TEST_POINTER_EQUALS(bsal_vector_at(&vector, 43), NULL);
        TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, 41), NULL);
        TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, 0), NULL);
        TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, 20), NULL);

        bsal_vector_resize(&vector, 100000);
        TEST_INT_EQUALS(bsal_vector_size(&vector), 100000);
        TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, 100000 - 1), NULL);
        TEST_POINTER_NOT_EQUALS(bsal_vector_at(&vector, 0), NULL);
        TEST_POINTER_EQUALS(bsal_vector_at(&vector, 2000000), NULL);

        bsal_vector_destroy(&vector);
    }

    {
        struct bsal_vector vector1;
        struct bsal_vector vector2;
        int i;
        void *buffer;
        int value1;
        int value2;
        int count;

        bsal_vector_init(&vector1, sizeof(int));

        for (i = 0; i < 99; i++) {
            bsal_vector_push_back(&vector1, &i);
        }

        count = bsal_vector_pack_size(&vector1);

        TEST_INT_IS_GREATER_THAN(count, 0);

        buffer = malloc(count);

        bsal_vector_pack(&vector1, buffer);
        bsal_vector_unpack(&vector2, buffer);

        for (i = 0; i < bsal_vector_size(&vector1); i++) {
            value1 = *(int *)bsal_vector_at(&vector1, i);
            value2 = *(int *)bsal_vector_at(&vector2, i);

            TEST_INT_EQUALS(value1, value2);
        }

        free(buffer);
    }

    END_TESTS();

    return 0;
}
