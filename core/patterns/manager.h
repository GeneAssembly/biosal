
#ifndef CORE_MANAGER_H
#define CORE_MANAGER_H

#include <engine/thorium/actor.h>

#include <core/structures/map.h>
#include <core/structures/vector.h>

#define SCRIPT_MANAGER 0xe41954c7

#define ACTION_GET_SPAWNERS 0x00065fc5
#define ACTION_GET_SPAWNERS_REPLY 0x002fd38f

#define ACTION_GET_SPAWNED_ACTORS 0x0009d5ff
#define ACTION_GET_SPAWNED_ACTORS_REPLY 0x0000424d

/**
 * How to use:
 *
 * 1. The script must be set with ACTION_MANAGER_SET_SCRIPT
 * (reply is ACTION_MANAGER_SET_SCRIPT_REPLY).
 *
 * 2. The number of actors per spawner is set with
 * ACTION_MANAGER_SET_ACTORS_PER_SPAWNER (reply is
 * ACTION_MANAGER_SET_ACTORS_PER_SPAWNER_REPLY). The default is to
 * spawn as many actors as there are workers on a spawner's
 * node.
 *
 * 3. The number of actors per worker can also be changed
 * with ACTION_MANAGER_SET_ACTORS_PER_WORKER (reply is
 * ACTION_MANAGER_SET_ACTORS_PER_WORKER_REPLY). The default is 1.
 *
 * 4. Finally, ACTION_START is sent to the manager (buffer contains
 * a vector of spawners). The manager
 * will reply with ACTION_START_REPLY and the buffer will
 * contain a vector of spawned actors (spawned using the provided spawners).
 *
 *
 */
struct core_manager {
    struct core_map spawner_children;
    struct core_map spawner_child_count;
    int ready_spawners;
    int spawner_count;
    struct core_vector indices;
    int script;

    int actors_per_spawner;
    int actors_per_worker;
    int workers_per_actor;

    struct core_vector children;
    struct core_vector spawners;
};

#define ACTION_MANAGER_SET_SCRIPT 0x00007595
#define ACTION_MANAGER_SET_SCRIPT_REPLY 0x00007667
#define ACTION_MANAGER_SET_ACTORS_PER_SPAWNER 0x000044d9
#define ACTION_MANAGER_SET_ACTORS_PER_SPAWNER_REPLY 0x00005b4c
#define ACTION_MANAGER_SET_ACTORS_PER_WORKER 0x000000c9
#define ACTION_MANAGER_SET_ACTORS_PER_WORKER_REPLY 0x00007b37
#define ACTION_MANAGER_SET_WORKERS_PER_ACTOR 0x0000322a
#define ACTION_MANAGER_SET_WORKERS_PER_ACTOR_REPLY 0x00007b74

extern struct thorium_script core_manager_script;

#endif
