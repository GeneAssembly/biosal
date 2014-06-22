
#ifndef BSAL_DYNAMIC_HASH_TABLE_H
#define BSAL_DYNAMIC_HASH_TABLE_H

#include "hash_table.h"
#include "hash_table_iterator.h"

struct bsal_dynamic_hash_table {
    struct bsal_hash_table table1;
    struct bsal_hash_table table2;

    /* information for resizing
     */
    struct bsal_hash_table_iterator iterator;
    struct bsal_hash_table *current;
    struct bsal_hash_table *next;

    int resize_in_progress;
};

void bsal_dynamic_hash_table_init(struct bsal_dynamic_hash_table *self, uint64_t buckets,
                int key_size, int value_size);
void bsal_dynamic_hash_table_destroy(struct bsal_dynamic_hash_table *self);

void *bsal_dynamic_hash_table_add(struct bsal_dynamic_hash_table *self, void *key);
void *bsal_dynamic_hash_table_get(struct bsal_dynamic_hash_table *self, void *key);
void bsal_dynamic_hash_table_delete(struct bsal_dynamic_hash_table *self, void *key);

uint64_t bsal_dynamic_hash_table_size(struct bsal_dynamic_hash_table *self);
uint64_t bsal_dynamic_hash_table_buckets(struct bsal_dynamic_hash_table *self);

/**
 * \return 1 if resizing was completed, 0 otherwise
 */
int bsal_dynamic_hash_table_resize(struct bsal_dynamic_hash_table *self);
void bsal_dynamic_hash_table_start_resizing(struct bsal_dynamic_hash_table *self);

int bsal_dynamic_hash_table_state(struct bsal_dynamic_hash_table *self, uint64_t bucket);
void *bsal_dynamic_hash_table_key(struct bsal_dynamic_hash_table *self, uint64_t bucket);
void *bsal_dynamic_hash_table_value(struct bsal_dynamic_hash_table *self, uint64_t bucket);

int bsal_dynamic_hash_table_pack_size(struct bsal_dynamic_hash_table *self);
int bsal_dynamic_hash_table_pack(struct bsal_dynamic_hash_table *self, void *buffer);
int bsal_dynamic_hash_table_unpack(struct bsal_dynamic_hash_table *self, void *buffer);

void bsal_dynamic_hash_table_finish_resizing(struct bsal_dynamic_hash_table *self);
void bsal_dynamic_hash_table_reset(struct bsal_dynamic_hash_table *self);

int bsal_dynamic_hash_table_get_key_size(struct bsal_dynamic_hash_table *self);
int bsal_dynamic_hash_table_get_value_size(struct bsal_dynamic_hash_table *self);

#endif
