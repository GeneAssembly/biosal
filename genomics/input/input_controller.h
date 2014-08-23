
#ifndef BSAL_INPUT_CONTROLLER_H
#define BSAL_INPUT_CONTROLLER_H

#include <engine/thorium/actor.h>

#include <core/structures/vector.h>
#include <core/structures/map.h>
#include <core/structures/queue.h>

#include <core/system/timer.h>

#include <genomics/data/dna_codec.h>


#define SCRIPT_INPUT_CONTROLLER 0x985607aa

struct bsal_input_controller {
    struct bsal_vector consumers;

    struct bsal_dna_codec codec;
    struct bsal_vector files;

    struct bsal_vector counting_streams;
    struct bsal_vector reading_streams;

    struct bsal_map mega_blocks;
    struct bsal_vector mega_block_vector;
    struct bsal_map assigned_blocks;

    struct bsal_vector spawners;
    int partitioner;

    int opened_streams;
    int counted;
    struct bsal_vector counts;
    struct bsal_vector partition_commands;
    struct bsal_vector stream_consumers;
    struct bsal_queue unprepared_spawners;
    int state;
    int block_size;
    int spawner;

    int ready_consumers;
    int filled_consumers;
    int ready_spawners;
    struct bsal_vector stores_per_spawner;
    int stores_per_worker_per_spawner;

    struct bsal_timer input_timer;
    struct bsal_timer counting_timer;
    struct bsal_timer distribution_timer;

    struct bsal_vector consumer_active_requests;
};

#define BSAL_INPUT_DISTRIBUTE 0x00003cbe
#define BSAL_INPUT_DISTRIBUTE_REPLY 0x00001ab0


#define BSAL_ADD_FILE 0x00007deb
#define BSAL_ADD_FILE_REPLY 0x00007036

#define BSAL_SET_BLOCK_SIZE 0x0000749e
#define BSAL_SET_BLOCK_SIZE_REPLY 0x00001461

#define BSAL_INPUT_SPAWN 0x00000a5d

#define BSAL_INPUT_CONTROLLER_CREATE_STORES 0x0000285f
#define BSAL_INPUT_CONTROLLER_CREATE_STORES_REPLY 0x000048ed
#define BSAL_INPUT_CONTROLLER_SPAWN_READING_STREAMS 0x00005341
#define BSAL_INPUT_CONTROLLER_PREPARE_SPAWNERS 0x00004a85
#define BSAL_INPUT_CONTROLLER_CREATE_PARTITION 0x00005b68

extern struct thorium_script bsal_input_controller_script;

void bsal_input_controller_init(struct thorium_actor *actor);
void bsal_input_controller_destroy(struct thorium_actor *actor);
void bsal_input_controller_receive(struct thorium_actor *actor, struct thorium_message *message);

void bsal_input_controller_create_stores(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_get_node_name_reply(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_get_node_worker_count_reply(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_add_store(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_prepare_spawners(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_receive_store_entry_counts(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_receive_command(struct thorium_actor *actor, struct thorium_message *message);

void bsal_input_controller_spawn_streams(struct thorium_actor *actor, struct thorium_message *message);
void bsal_input_controller_set_offset_reply(struct thorium_actor *self, struct thorium_message *message);
void bsal_input_controller_verify_requests(struct thorium_actor *self, struct thorium_message *message);

#endif
