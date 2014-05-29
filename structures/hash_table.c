
#include "hash_table.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define BSAL_HASH_TABLE_KEY_NOT_FOUND 0
#define BSAL_HASH_TABLE_KEY_FOUND 1
#define BSAL_HASH_TABLE_FULL 2

#define BSAL_HASH_TABLE_OPERATION_ADD 0
#define BSAL_HASH_TABLE_OPERATION_GET 1
#define BSAL_HASH_TABLE_OPERATION_DELETE 2

#define BSAL_HASH_TABLE_MATCH 0

/*#define BSAL_HASH_TABLE_DEBUG*/

void bsal_hash_table_init(struct bsal_hash_table *table, uint64_t buckets,
                int key_size, int value_size)
{
    int i;
    int buckets_per_group;

    /* google sparsehash uses 48. 64 is nice too */
    buckets_per_group = 64;

    while (buckets % buckets_per_group != 0) {
        buckets++;
    }

    table->buckets = buckets;
    table->buckets_per_group = buckets_per_group;
    table->key_size = key_size;
    table->value_size = value_size;

    table->elements = 0;
    table->group_count = (buckets / buckets_per_group);

    table->groups = (struct bsal_hash_table_group *)
            malloc(table->group_count * sizeof(struct bsal_hash_table_group));

    for (i = 0; i < table->group_count; i++) {
        bsal_hash_table_group_init(table->groups + i, buckets_per_group,
                        key_size, value_size);
    }
}

void bsal_hash_table_destroy(struct bsal_hash_table *table)
{
    int i;

    for (i = 0; i < table->group_count; i++) {
        bsal_hash_table_group_destroy(table->groups + i);
    }

    free(table->groups);
    table->groups = NULL;
}

void *bsal_hash_table_add(struct bsal_hash_table *table, void *key)
{
    int group;
    int bucket_in_group;
    void *bucket_key;
    int code;

    code = bsal_hash_table_find_bucket(table, key, &group, &bucket_in_group,
                    BSAL_HASH_TABLE_OPERATION_ADD);

    if (code == BSAL_HASH_TABLE_KEY_NOT_FOUND) {

#ifdef BSAL_HASH_TABLE_DEBUG
        printf("bsal_hash_table_add code BSAL_HASH_TABLE_KEY_NOT_FOUND"
                        "(group %i bucket %i)\n", group, bucket_in_group);
#endif

        /* install the key */
        bucket_key = bsal_hash_table_group_key(table->groups + group,
                        bucket_in_group, table->key_size, table->value_size);

        /*printf("memcpy %p %p %i\n", bucket_key, key, table->key_size); */
        memcpy(bucket_key, key, table->key_size);
        table->elements++;

        return bsal_hash_table_group_add(table->groups + group, bucket_in_group,
                   table->key_size, table->value_size);

    } else if (code == BSAL_HASH_TABLE_KEY_FOUND) {

#ifdef BSAL_HASH_TABLE_DEBUG
        printf("bsal_hash_table_add code BSAL_HASH_TABLE_KEY_FOUND"
                        "(group %i bucket %i)\n", group, bucket_in_group);
#endif

        return bsal_hash_table_group_get(table->groups + group, bucket_in_group,
                   table->key_size, table->value_size);

    } else if (code == BSAL_HASH_TABLE_FULL) {

#ifdef BSAL_HASH_TABLE_DEBUG
        printf("bsal_hash_table_add code BSAL_HASH_TABLE_FULL\n");
#endif

        return NULL;
    }

    /* this statement is unreachable.
     */
    return NULL;
}

void *bsal_hash_table_get(struct bsal_hash_table *table, void *key)
{
    int group;
    int bucket_in_group;
    int code;

    code = bsal_hash_table_find_bucket(table, key, &group, &bucket_in_group,
                    BSAL_HASH_TABLE_OPERATION_GET);

    /* bsal_hash_table_group_get would return NULL too,
     * but using this return code is cleaner */
    if (code == BSAL_HASH_TABLE_KEY_NOT_FOUND) {
        return NULL;
    }

#ifdef BSAL_HASH_TABLE_DEBUG
    printf("get %i %i code %i\n", group, bucket_in_group,
                    code);
#endif

    return bsal_hash_table_group_get(table->groups + group, bucket_in_group,
                    table->key_size, table->value_size);
}

void bsal_hash_table_delete(struct bsal_hash_table *table, void *key)
{
    int group;
    int bucket_in_group;
    int code;

    code = bsal_hash_table_find_bucket(table, key, &group, &bucket_in_group,
                    BSAL_HASH_TABLE_OPERATION_DELETE);

    if (code == BSAL_HASH_TABLE_KEY_FOUND) {

#ifdef BSAL_HASH_TABLE_DEBUG
        printf("bsal_hash_table_delete code BSAL_HASH_TABLE_KEY_FOUND %i %i\n",
                        group, bucket_in_group);
#endif

        bsal_hash_table_group_delete(table->groups + group, bucket_in_group);
        table->elements--;
    }
}

int bsal_hash_table_get_group(struct bsal_hash_table *table, uint64_t bucket)
{
    return bucket / table->buckets_per_group;
}

int bsal_hash_table_get_group_bucket(struct bsal_hash_table *table, uint64_t bucket)
{
    return bucket % table->buckets_per_group;
}

/* \see http://en.wikipedia.org/wiki/MurmurHash
 * \see http://www.maatkit.org/
 * \see https://code.google.com/p/maatkit/issues/attachmentText?id=19&aid=7029841249934490324&name=MurmurHash64.cpp&token=3b615cc6c16c91de800419e5e95ed1ba
 */
uint64_t bsal_murmur_hash_64(const void *key, int len, unsigned int seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ len;

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = data + (len/8);

    while (data != end) {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char * data2 = (const unsigned char*)data;

    switch (len & 7) {
        case 7: h ^= (uint64_t)(data2[6]) << 48;
        case 6: h ^= (uint64_t)(data2[5]) << 40;
        case 5: h ^= (uint64_t)(data2[4]) << 32;
        case 4: h ^= (uint64_t)(data2[3]) << 24;
        case 3: h ^= (uint64_t)(data2[2]) << 16;
        case 2: h ^= (uint64_t)(data2[1]) << 8;
        case 1: h ^= (uint64_t)(data2[0]);
                h *= m;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

uint64_t bsal_hash_table_hash1(struct bsal_hash_table *table, void *key)
{
    return bsal_murmur_hash_64(key, table->key_size, 0x5cd902cb);
}

uint64_t bsal_hash_table_hash2(struct bsal_hash_table *table, void *key)
{
    uint64_t value;

    value = bsal_murmur_hash_64(key, table->key_size, 0x80435418);

    /* the number of buckets and hash2 must be co-prime
     * the number of buckets is a power of 2
     */
    if (value % 2 == 0) {
        value++;
    }

    return value;
}

uint64_t bsal_hash_table_double_hash(struct bsal_hash_table *table, void *key, uint64_t stride)
{
    uint64_t hash1;
    uint64_t hash2;
    uint64_t result;

    hash2 = 0;
    hash1 = bsal_hash_table_hash1(table, key);

    if (stride > 0) {
        hash2 = bsal_hash_table_hash2(table, key);
    }

    result = (hash1 + stride * hash2) % table->buckets;

    return result;
}

/* this is the most important function for the hash table.
 * it finds a bucket with a key
 *
 * \param operation is one of these: BSAL_HASH_TABLE_OPERATION_ADD,
 * BSAL_HASH_TABLE_OPERATION_GET, BSAL_HASH_TABLE_OPERATION_DELETE
 *
 * \return value is BSAL_HASH_TABLE_KEY_FOUND or BSAL_HASH_TABLE_KEY_NOT_FOUND or
 * BSAL_HASH_TABLE_FULL
 */
int bsal_hash_table_find_bucket(struct bsal_hash_table *table, void *key,
                int *group, int *bucket_in_group, int operation)
{
    uint64_t stride;
    uint64_t bucket;
    int state;
    struct bsal_hash_table_group *hash_group;
    void *bucket_key;

    stride = 0;

    while (stride < table->buckets) {

        bucket = bsal_hash_table_double_hash(table, key, stride);
        *group = bsal_hash_table_get_group(table, bucket);
        *bucket_in_group = bsal_hash_table_get_group_bucket(table, bucket);
        hash_group = table->groups + *group;

        state = bsal_hash_table_group_state(hash_group, *bucket_in_group);

        /* nothing to see here, it is deleted !
         * we only pick it up for BSAL_HASH_TABLE_OPERATION_ADD
         * \see http://webdocs.cs.ualberta.ca/~holte/T26/open-addr.html
         */
        if (state == BSAL_HASH_TABLE_BUCKET_DELETED
              && (operation == BSAL_HASH_TABLE_OPERATION_DELETE
                      ||
                  operation == BSAL_HASH_TABLE_OPERATION_GET)) {
            stride++;
            continue;
        }

        /* a deleted bucket was found, it can be used to add
         * an item.
         */
        if (state == BSAL_HASH_TABLE_BUCKET_DELETED
               && operation == BSAL_HASH_TABLE_OPERATION_ADD) {
            return BSAL_HASH_TABLE_KEY_NOT_FOUND;
        }

        /* we found an empty bucket to fulfil the procurement.
         */
        if (state == BSAL_HASH_TABLE_BUCKET_EMPTY) {
            return BSAL_HASH_TABLE_KEY_NOT_FOUND;
        }

        /* the bucket is occupied, compare it with the key */
        bucket_key = bsal_hash_table_group_key(hash_group, *bucket_in_group,
                        table->key_size, table->value_size);

        /*
         * we found a key, check if it matches the query.
         */
        if (state == BSAL_HASH_TABLE_BUCKET_OCCUPIED
                && memcmp(bucket_key, key, table->key_size) ==
                BSAL_HASH_TABLE_MATCH) {
            return BSAL_HASH_TABLE_KEY_FOUND;
        }

        /* otherwise, continue the search
         */
        stride++;
    }

    /* this statement will only be reached when the table is already full,
     * or if a key was not found and the table is full
     */

    if (operation == BSAL_HASH_TABLE_OPERATION_ADD) {
        return BSAL_HASH_TABLE_FULL;
    }

    if (operation == BSAL_HASH_TABLE_OPERATION_GET
                    || operation == BSAL_HASH_TABLE_OPERATION_DELETE) {
        return BSAL_HASH_TABLE_KEY_NOT_FOUND;
    }

    return BSAL_HASH_TABLE_FULL;
}

int bsal_hash_table_elements(struct bsal_hash_table *table)
{
    return table->elements;
}

int bsal_hash_table_buckets(struct bsal_hash_table *table)
{
    return table->buckets;
}
