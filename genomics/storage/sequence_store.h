
#ifndef BIOSAL_SEQUENCE_STORE_H
#define BIOSAL_SEQUENCE_STORE_H

#include <genomics/data/dna_codec.h>

#include <core/structures/vector.h>
#include <core/structures/vector_iterator.h>

#include <core/system/memory_pool.h>

#include <engine/thorium/actor.h>

#define SCRIPT_SEQUENCE_STORE 0x47e2e424

/*
 * This is a BIOSAL DNA sequence store.
 *
 * It is possibler to distribute data by using a lot of these actors.
 */
struct biosal_sequence_store {
    struct core_vector sequences;
    struct biosal_dna_codec codec;
    int64_t received;
    int64_t expected;

    int iterator_started;
    int reservation_producer;
    struct core_vector_iterator iterator;

    int64_t left;
    int64_t last;

    struct core_memory_pool persistent_memory;

    int progress_supervisor;

    int required_kmers;

    int production_block_size;
};

#define ACTION_RESERVE 0x00000d3c
#define ACTION_RESERVE_REPLY 0x00002ca8
#define ACTION_PUSH_SEQUENCE_DATA_BLOCK 0x00001160
#define ACTION_PUSH_SEQUENCE_DATA_BLOCK_REPLY 0x00004d02

#define ACTION_SEQUENCE_STORE_READY 0x00002c00

#define ACTION_SEQUENCE_STORE_ASK 0x00006b99
#define ACTION_SEQUENCE_STORE_ASK_REPLY 0x00007b13

#define ACTION_SEQUENCE_STORE_REQUEST_PROGRESS 0x0000648a
#define ACTION_SEQUENCE_STORE_REQUEST_PROGRESS_REPLY 0x000074a5

extern struct thorium_script biosal_sequence_store_script;

#endif
