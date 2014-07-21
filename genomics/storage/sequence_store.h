
#ifndef BSAL_SEQUENCE_STORE_H
#define BSAL_SEQUENCE_STORE_H

#include <engine/thorium/actor.h>
#include <genomics/data/dna_codec.h>
#include <core/structures/vector.h>
#include <core/structures/vector_iterator.h>

#include <core/system/memory_pool.h>

#define BSAL_SEQUENCE_STORE_SCRIPT 0x47e2e424

struct bsal_sequence_store {
    struct bsal_vector sequences;
    struct bsal_dna_codec codec;
    int64_t received;
    int64_t expected;

    int iterator_started;
    int reservation_producer;
    struct bsal_vector_iterator iterator;

    int64_t left;
    int64_t last;

    struct bsal_memory_pool persistent_memory;

    int progress_supervisor;
};

#define BSAL_RESERVE 0x00000d3c
#define BSAL_RESERVE_REPLY 0x00002ca8
#define BSAL_PUSH_SEQUENCE_DATA_BLOCK 0x00001160
#define BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY 0x00004d02

#define BSAL_SEQUENCE_STORE_READY 0x00002c00

#define BSAL_SEQUENCE_STORE_ASK 0x00006b99
#define BSAL_SEQUENCE_STORE_ASK_REPLY 0x00007b13

#define BSAL_SEQUENCE_STORE_REQUEST_PROGRESS 0x0000648a
#define BSAL_SEQUENCE_STORE_REQUEST_PROGRESS_REPLY 0x000074a5

/*
 * The block size is difference depending on the nature of the work.
 */
#define BSAL_SEQUENCE_STORE_PRODUCT_BLOCK_SIZE 32
#define BSAL_SEQUENCE_STORE_PRODUCT_BLOCK_SIZE_DISTRIBUTED BSAL_SEQUENCE_STORE_PRODUCT_BLOCK_SIZE
#define BSAL_SEQUENCE_STORE_PRODUCT_BLOCK_SIZE_ONE_NODE BSAL_SEQUENCE_STORE_PRODUCT_BLOCK_SIZE

extern struct bsal_script bsal_sequence_store_script;

void bsal_sequence_store_init(struct bsal_actor *actor);
void bsal_sequence_store_destroy(struct bsal_actor *actor);
void bsal_sequence_store_receive(struct bsal_actor *actor, struct bsal_message *message);

int bsal_sequence_store_has_error(struct bsal_actor *actor,
                struct bsal_message *message);

int bsal_sequence_store_check_open_error(struct bsal_actor *actor,
                struct bsal_message *message);
void bsal_sequence_store_push_sequence_data_block(struct bsal_actor *actor, struct bsal_message *message);
void bsal_sequence_store_reserve(struct bsal_actor *actor, struct bsal_message *message);
void bsal_sequence_store_show_progress(struct bsal_actor *actor, struct bsal_message *message);

void bsal_sequence_store_ask(struct bsal_actor *self, struct bsal_message *message);

#endif
