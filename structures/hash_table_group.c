
#include "hash_table_group.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*#define BSAL_HASH_TABLE_GROUP_DEBUG */

#define BSAL_BIT_ZERO 0
#define BSAL_BIT_ONE 1

#define BSAL_BITS_PER_BYTE 8

void bsal_hash_table_group_init(struct bsal_hash_table_group *group,
                int buckets_per_group, int key_size, int value_size)
{
    int bitmap_bytes;
    int array_bytes;

    bitmap_bytes = buckets_per_group / BSAL_BITS_PER_BYTE;
    array_bytes = buckets_per_group * (key_size + value_size);

    /* TODO: use slab allocator */
    group->array = malloc(array_bytes);
    group->occupancy_bitmap = malloc(bitmap_bytes);
    group->deletion_bitmap = malloc(bitmap_bytes);

    /* mark all buckets as not occupied */
    memset(group->occupancy_bitmap, BSAL_BIT_ZERO, bitmap_bytes);
    memset(group->deletion_bitmap, BSAL_BIT_ZERO, bitmap_bytes);
}

void bsal_hash_table_group_destroy(struct bsal_hash_table_group *group)
{
    /* TODO use slab allocator */
    free(group->occupancy_bitmap);
    group->occupancy_bitmap = NULL;

    free(group->deletion_bitmap);
    group->deletion_bitmap = NULL;

    free(group->array);
    group->array = NULL;
}

void bsal_hash_table_group_delete(struct bsal_hash_table_group *group, int bucket)
{
#ifdef BSAL_HASH_TABLE_GROUP_DEBUG
    printf("bsal_hash_table_group_delete setting occupancy to 0 and deleted to 1 for %i\n"
                    bucket);
#endif

    bsal_hash_table_group_set_bit(group->occupancy_bitmap, bucket,
                    BSAL_BIT_ZERO);
    bsal_hash_table_group_set_bit(group->deletion_bitmap, bucket,
                    BSAL_BIT_ONE);
}

void *bsal_hash_table_group_add(struct bsal_hash_table_group *group,
                int bucket, int key_size, int value_size)
{
    bsal_hash_table_group_set_bit(group->occupancy_bitmap, bucket,
                    BSAL_BIT_ONE);
    bsal_hash_table_group_set_bit(group->deletion_bitmap, bucket,
                    BSAL_BIT_ZERO);

    return (char *)group->array + bucket * (key_size + value_size);
}

void *bsal_hash_table_group_get(struct bsal_hash_table_group *group,
                int bucket, int key_size, int value_size)
{
    int offset;

    if (bsal_hash_table_group_state(group, bucket) ==
                    BSAL_HASH_TABLE_BUCKET_OCCUPIED) {

        offset = bucket * (key_size + value_size) + key_size;
        return (char *)group->array + offset;
    }

    /* BSAL_HASH_TABLE_BUCKET_EMPTY and BSAL_HASH_TABLE_BUCKET_DELETED
     * are not occupied
     */
    return NULL;
}

/*
 * returns BSAL_HASH_TABLE_BUCKET_EMPTY or
 * BSAL_HASH_TABLE_BUCKET_OCCUPIED or
 * BSAL_HASH_TABLE_BUCKET_DELETED
 */
int bsal_hash_table_group_state(struct bsal_hash_table_group *group, int bucket)
{
    if (bsal_hash_table_group_get_bit(group->occupancy_bitmap, bucket) == 1) {
        return BSAL_HASH_TABLE_BUCKET_OCCUPIED;
    }

    if (bsal_hash_table_group_get_bit(group->deletion_bitmap, bucket) == 1) {
        return BSAL_HASH_TABLE_BUCKET_DELETED;
    }

    return BSAL_HASH_TABLE_BUCKET_EMPTY;
}

void bsal_hash_table_group_set_bit(void *bitmap, int bucket, int value1)
{
    int unit;
    int bit;
    uint64_t bits;
    uint64_t value;

    value = value1;
    unit = bucket / BSAL_BITS_PER_BYTE;
    bit = bucket % BSAL_BITS_PER_BYTE;

    bits = (uint64_t)((char *)bitmap)[unit];

    if (value == BSAL_BIT_ONE){
        bits |= (value << bit);

        /* set bit to 0 */
    } else if (value == BSAL_BIT_ZERO) {
        uint64_t filter = BSAL_BIT_ONE;
        filter <<= bit;
        filter =~ filter;
        bits &= filter;
    }

    ((char *)bitmap)[unit] = bits;
}

int bsal_hash_table_group_get_bit(void *bitmap, int bucket)
{
    int unit;
    int bit;
    uint64_t bits;
    int bitValue;

    unit = bucket / BSAL_BITS_PER_BYTE;
    bit = bucket % BSAL_BITS_PER_BYTE;

    /*printf("bsal_hash_table_group_get_bit %p %i\n", group->bitmap, unit);*/

    bits = (uint64_t)((char *)bitmap)[unit];
    bitValue = (bits<<(63 - bit))>>63;

    return bitValue;
}

void *bsal_hash_table_group_key(struct bsal_hash_table_group *group, int bucket,
                int key_size, int value_size)
{
    /* we assume that the key is stored first */
    return (char *)group->array + bucket * (key_size + value_size);
}
