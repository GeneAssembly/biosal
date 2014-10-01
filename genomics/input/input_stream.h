
#ifndef BIOSAL_INPUT_STREAM_H
#define BIOSAL_INPUT_STREAM_H

#include <genomics/formats/input_proxy.h>

#include <genomics/data/dna_codec.h>

#include <engine/thorium/actor.h>

#include <core/system/memory_pool.h>

#include <core/structures/string.h>

#define SCRIPT_INPUT_STREAM 0xeb2fe16a

struct biosal_input_stream {
    struct biosal_input_proxy proxy;
    struct biosal_dna_codec codec;
    int proxy_ready;
    char *buffer_for_sequence;
    int maximum_sequence_length;
    int open;
    int controller;
    int error;
    int64_t mega_block_size;
    int granularity;

    int64_t last_offset;
    int64_t last_entries;

    char *file_name;

    struct core_vector mega_blocks;

    uint64_t starting_offset;
    uint64_t ending_offset;

    int count_customer;

#if 0
    struct core_string file_for_parallel_counting;
#endif

    /*
     * Parallel counting.
     */

    uint64_t total_entries;
    int finished_parallel_stream_count;
    struct core_vector spawners;
    struct core_vector parallel_streams;
    struct core_vector start_offsets;
    struct core_vector end_offsets;
    struct core_vector parallel_mega_blocks;
};

#define ACTION_INPUT_OPEN 0x000075fa
#define ACTION_INPUT_OPEN_REPLY 0x00000f68
#define ACTION_INPUT_COUNT 0x00004943
#define ACTION_INPUT_COUNT_PROGRESS 0x00001ce0
#define ACTION_INPUT_COUNT_READY 0x0000710e
#define ACTION_INPUT_COUNT_REPLY 0x000018a9
#define ACTION_INPUT_CLOSE 0x00007646
#define ACTION_INPUT_CLOSE_REPLY 0x00004329

#define ACTION_INPUT_STREAM_SET_START_OFFSET 0x000041d5
#define ACTION_INPUT_STREAM_SET_START_OFFSET_REPLY 0x0000233a

#define ACTION_INPUT_STREAM_SET_END_OFFSET 0x00006670
#define ACTION_INPUT_STREAM_SET_END_OFFSET_REPLY 0x00005f30

#define ACTION_INPUT_STREAM_RESET 0x00007869
#define ACTION_INPUT_STREAM_RESET_REPLY 0x00002c63
#define ACTION_INPUT_GET_SEQUENCE 0x00001333
#define ACTION_INPUT_GET_SEQUENCE_END 0x00006d55
#define ACTION_INPUT_GET_SEQUENCE_REPLY 0x00005295

#define ACTION_INPUT_PUSH_SEQUENCES 0x00005e48
#define ACTION_INPUT_PUSH_SEQUENCES_READY 0x000001d2
#define ACTION_INPUT_PUSH_SEQUENCES_REPLY 0x0000695c

#define ACTION_INPUT_COUNT_IN_PARALLEL 0x00001ce9
#define ACTION_INPUT_COUNT_IN_PARALLEL_REPLY 0x000058ea

extern struct thorium_script biosal_input_stream_script;

void biosal_input_stream_init(struct thorium_actor *actor);
void biosal_input_stream_destroy(struct thorium_actor *actor);
void biosal_input_stream_receive(struct thorium_actor *actor, struct thorium_message *message);

void biosal_input_stream_send_sequences_to(struct thorium_actor *actor,
                struct thorium_message *message);

int biosal_input_stream_has_error(struct thorium_actor *actor,
                struct thorium_message *message);

int biosal_input_stream_check_open_error(struct thorium_actor *actor,
                struct thorium_message *message);
void biosal_input_stream_push_sequences(struct thorium_actor *actor,
                struct thorium_message *message);

void biosal_input_stream_set_start_offset(struct thorium_actor *actor, struct thorium_message *message);
void biosal_input_stream_set_end_offset(struct thorium_actor *actor, struct thorium_message *message);

void biosal_input_stream_count_in_parallel(struct thorium_actor *self, struct thorium_message *message);
void biosal_input_stream_count_reply(struct thorium_actor *self, struct thorium_message *message);

void biosal_input_stream_count_reply_mock(struct thorium_actor *self, struct thorium_message *message);
void biosal_input_stream_count_in_parallel_mock(struct thorium_actor *self, struct thorium_message *message);
void biosal_input_stream_spawn_reply(struct thorium_actor *self, struct thorium_message *message);
void biosal_input_stream_open_reply(struct thorium_actor *self, struct thorium_message *message);
void biosal_input_stream_set_offset_reply(struct thorium_actor *self, struct thorium_message *message);

#endif
