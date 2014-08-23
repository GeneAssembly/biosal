
#ifndef BSAL_MANAGER_H
#define BSAL_MANAGER_H

#include <engine/thorium/actor.h>

#include <core/structures/map.h>
#include <core/structures/vector.h>

#define SCRIPT_MANAGER 0xe41954c7

/**
 * How to use:
 *
 * 1. The script must be set with BSAL_MANAGER_SET_SCRIPT
 * (reply is BSAL_MANAGER_SET_SCRIPT_REPLY).
 *
 * 2. The number of actors per spawner is set with
 * BSAL_MANAGER_SET_ACTORS_PER_SPAWNER (reply is
 * BSAL_MANAGER_SET_ACTORS_PER_SPAWNER_REPLY). The default is to
 * spawn as many actors as there are workers on a spawner's
 * node.
 *
 * 3. The number of actors per worker can also be changed
 * with BSAL_MANAGER_SET_ACTORS_PER_WORKER (reply is
 * BSAL_MANAGER_SET_ACTORS_PER_WORKER_REPLY). The default is 1.
 *
 * 4. Finally, THORIUM_ACTOR_START is sent to the manager (buffer contains
 * a vector of spawners). The manager
 * will reply with THORIUM_ACTOR_START_REPLY and the buffer will
 * contain a vector of spawned actors (spawned using the provided spawners).
 *
 *
 */
struct bsal_manager {
    struct bsal_map spawner_children;
    struct bsal_map spawner_child_count;
    int ready_spawners;
    int spawners;
    struct bsal_vector indices;
    int script;

    int actors_per_spawner;
    int actors_per_worker;
    int workers_per_actor;

    struct bsal_vector children;
};

#define BSAL_MANAGER_SET_SCRIPT 0x00007595
#define BSAL_MANAGER_SET_SCRIPT_REPLY 0x00007667
#define BSAL_MANAGER_SET_ACTORS_PER_SPAWNER 0x000044d9
#define BSAL_MANAGER_SET_ACTORS_PER_SPAWNER_REPLY 0x00005b4c
#define BSAL_MANAGER_SET_ACTORS_PER_WORKER 0x000000c9
#define BSAL_MANAGER_SET_ACTORS_PER_WORKER_REPLY 0x00007b37
#define BSAL_MANAGER_SET_WORKERS_PER_ACTOR 0x0000322a
#define BSAL_MANAGER_SET_WORKERS_PER_ACTOR_REPLY 0x00007b74

extern struct thorium_script bsal_manager_script;

void bsal_manager_init(struct thorium_actor *self);
void bsal_manager_destroy(struct thorium_actor *self);
void bsal_manager_receive(struct thorium_actor *self, struct thorium_message *message);

void bsal_manager_ask_to_stop(struct thorium_actor *actor, struct thorium_message *message);

#endif
