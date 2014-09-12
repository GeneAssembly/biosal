
#include "unitig_manager.h"

#include "unitig_visitor.h"
#include "unitig_walker.h"

#include <core/patterns/manager.h>

#define UNITIG_WALKER_COUNT_PER_WORKER 512
#define UNITIG_VISITOR_COUNT_PER_WORKER 512

struct thorium_script bsal_unitig_manager_script = {
    .identifier = SCRIPT_UNITIG_MANAGER,
    .name = "bsal_unitig_manager",
    .init = bsal_unitig_manager_init,
    .destroy = bsal_unitig_manager_destroy,
    .receive = bsal_unitig_manager_receive,
    .size = sizeof(struct bsal_unitig_manager),
    .description = "The chief executive for unitig walkers"
};

void bsal_unitig_manager_init(struct thorium_actor *self)
{
    struct bsal_unitig_manager *concrete_self;

    concrete_self = (struct bsal_unitig_manager *)thorium_actor_concrete_actor(self);

    bsal_vector_init(&concrete_self->spawners, sizeof(int));
    bsal_vector_init(&concrete_self->graph_stores, sizeof(int));

    bsal_vector_init(&concrete_self->visitors, sizeof(int));
    bsal_vector_init(&concrete_self->walkers, sizeof(int));

    concrete_self->completed = 0;
    concrete_self->manager = THORIUM_ACTOR_NOBODY;

    bsal_timer_init(&concrete_self->timer);
}

void bsal_unitig_manager_destroy(struct thorium_actor *self)
{
    struct bsal_unitig_manager *concrete_self;

    concrete_self = (struct bsal_unitig_manager *)thorium_actor_concrete_actor(self);

    bsal_vector_destroy(&concrete_self->spawners);
    bsal_vector_destroy(&concrete_self->graph_stores);

    bsal_vector_destroy(&concrete_self->walkers);
    bsal_vector_destroy(&concrete_self->visitors);

    concrete_self->completed = 0;
    concrete_self->manager = THORIUM_ACTOR_NOBODY;

    bsal_timer_destroy(&concrete_self->timer);
}

/*
 * Basically, this actor does this:
 * - spawn visitors
 * - let them visit stuff
 * - kill them.
 * - spawn walkers
 * - let them walk
 * - kill the walkers
 * - return OK
 */
void bsal_unitig_manager_receive(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_unitig_manager *concrete_self;
    int tag;
    void *buffer;
    int spawner;

    tag = thorium_message_action(message);
    buffer = thorium_message_buffer(message);

    concrete_self = (struct bsal_unitig_manager *)thorium_actor_concrete_actor(self);

    if (tag == ACTION_START) {

        bsal_vector_unpack(&concrete_self->spawners, buffer);

        spawner = thorium_actor_get_random_spawner(self, &concrete_self->spawners);

        thorium_actor_send_int(self, spawner, ACTION_SPAWN, SCRIPT_MANAGER);

    } else if (tag == ACTION_SPAWN_REPLY) {

        thorium_message_unpack_int(message, 0, &concrete_self->manager);

        thorium_actor_send_int(self, concrete_self->manager, ACTION_MANAGER_SET_SCRIPT,
                        SCRIPT_UNITIG_VISITOR);

    } else if (tag == ACTION_ASK_TO_STOP) {

        thorium_actor_send_to_self_empty(self, ACTION_STOP);

        thorium_actor_send_empty(self, concrete_self->manager,
                        ACTION_ASK_TO_STOP);

        thorium_actor_send_reply_empty(self, ACTION_ASK_TO_STOP_REPLY);

    } else if (tag == ACTION_MANAGER_SET_SCRIPT_REPLY) {

        thorium_actor_send_reply_int(self, ACTION_MANAGER_SET_ACTORS_PER_WORKER,
                        UNITIG_VISITOR_COUNT_PER_WORKER);

    } else if (tag == ACTION_MANAGER_SET_ACTORS_PER_WORKER_REPLY) {

        thorium_actor_send_reply_vector(self, ACTION_START,
                        &concrete_self->spawners);

    } else if (tag == ACTION_START_REPLY
                    && bsal_vector_size(&concrete_self->visitors) == 0) {

        bsal_vector_unpack(&concrete_self->visitors, buffer);

        printf("DEBUG the system has %d visitors\n",
                        (int)bsal_vector_size(&concrete_self->visitors));

        thorium_actor_send_to_supervisor_empty(self, ACTION_START_REPLY);

    } else if (tag == ACTION_SET_PRODUCERS) {

        bsal_vector_unpack(&concrete_self->graph_stores, buffer);

        bsal_timer_start(&concrete_self->timer);

        thorium_actor_send_range_vector(self, &concrete_self->visitors,
                        ACTION_START, &concrete_self->graph_stores);

    } else if (tag == ACTION_START_REPLY) {

        ++concrete_self->completed;

        printf("PROGRESS unitig visitors %d/%d\n",
                        concrete_self->completed,
                        (int)bsal_vector_size(&concrete_self->visitors));

        if (concrete_self->completed == bsal_vector_size(&concrete_self->visitors)) {

            bsal_timer_stop(&concrete_self->timer);
            bsal_timer_print_with_description(&concrete_self->timer, "Traverse graph for unitigs");

            thorium_actor_send_to_supervisor_empty(self, ACTION_SET_PRODUCERS_REPLY);
        }
    }
}

