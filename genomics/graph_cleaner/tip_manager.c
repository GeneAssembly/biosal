
#include "tip_manager.h"

#include <core/helpers/integer.h>

#include <core/structures/vector.h>
#include <core/patterns/manager.h>
#include <engine/thorium/actor.h>

#include <stdio.h>

void tip_manager_init(struct thorium_actor *self);
void tip_manager_destroy(struct thorium_actor *self);
void tip_manager_receive(struct thorium_actor *self, struct thorium_message *message);

struct thorium_script biosal_tip_manager_script = {
    .identifier = SCRIPT_TIP_MANAGER,
    .init = tip_manager_init,
    .destroy = tip_manager_destroy,
    .receive = tip_manager_receive,
    .size = sizeof(struct biosal_tip_manager),
    .name = "biosal_tip_manager",
};

void tip_manager_init(struct thorium_actor *self)
{
    struct biosal_tip_manager *concrete_self;
    concrete_self = thorium_actor_concrete_actor(self);

    core_vector_init(&concrete_self->spawners, sizeof(int));
}

void tip_manager_destroy(struct thorium_actor *self)
{
    struct biosal_tip_manager *concrete_self;
    concrete_self = thorium_actor_concrete_actor(self);

    core_vector_destroy(&concrete_self->spawners);
}

void tip_my_callback(struct thorium_actor *self, struct thorium_message *message)
{
    /* do nothing */
}

void tip_manager_callback_x(struct thorium_actor *self, struct thorium_message *message);

void tip_manager_receive(struct thorium_actor *self, struct thorium_message *message)
{
    struct biosal_tip_manager *concrete_self;
    int action;
    void *buffer;
    int graph_manager_name;

    concrete_self = thorium_actor_concrete_actor(self);

    action = thorium_message_action(message);
    buffer = BUFFER(message);

    if (action == ACTION_START) {

        concrete_self->__supervisor = SOURCE(message);

        core_int_unpack(&concrete_self->graph_manager_name, buffer);

        LOG("tip manager receives ACTION_START, graph manager is %d\n",
                        concrete_self->graph_manager_name);

        struct thorium_message new_message;
        thorium_message_init(&new_message, ACTION_TEST, 0, NULL);

        ASK(MSG, NAME(), &new_message, tip_manager_callback_x);

#if 0
        int test_value = 33;

        int destination = NAME();
        struct core_vector vector;
        core_vector_init(&vector, sizeof(int));

        TELL(1, destination, ACTION_TEST, TYPE_INT, 9);
        TELL(1, destination, ACTION_TEST, TYPE_VECTOR, &vector);

        TELL(2, destination, ACTION_TEST, TYPE_INT, 9, TYPE_VECTOR, &vector);
        /*
*/
        core_vector_destroy(&vector);

        TELL(1, NAME(), ACTION_TEST, TYPE_INT, test_value);


#endif
        concrete_self->done = false;

    } else if (action == ACTION_TEST
                    && !concrete_self->done) {

        concrete_self->done = true;

        /*
         * - Spawn a manager
         * - Ask it to spawn tip detectors
         * - Bind them with graph stores (ACTION_SET_PRODUCER)
         */
        /*
        LOG("Removed tips !!");
        */

        REPLY(0, ACTION_TEST);

#if 0
        int spawner = thorium_actor_get_random_spawner(self,
                        &concrete_self->spawners);

        TELL(0, NAME(), ACTION_TEST);

        struct core_vector vector;
        core_vector_init(&vector, sizeof(int));
        struct thorium_message new_message;
        int data = 99;
        thorium_message_init(&new_message, ACTION_TEST, sizeof(data), &data);
        ASK(MSG, NAME(), &new_message, tip_my_callback);
        core_vector_destroy(&vector);

#endif
        /*
         * Also, kill self.
         */
    } else if (action == ACTION_ASK_TO_STOP) {

        thorium_actor_send_reply_empty(self, ACTION_ASK_TO_STOP_REPLY);

        thorium_actor_send_to_self_empty(self, ACTION_STOP);
    } else {
        thorium_actor_take_action(self, message);
    }

}

void tip_manager_callback_y(struct thorium_actor *self, struct thorium_message *message);

void tip_manager_callback_x(struct thorium_actor *self, struct thorium_message *message)
{
    struct biosal_tip_manager *concrete_self;
    concrete_self = thorium_actor_concrete_actor(self);


    ASK(0, concrete_self->graph_manager_name, ACTION_GET_SPAWNERS, tip_manager_callback_y);
}

void tip_manager_callback_y(struct thorium_actor *self, struct thorium_message *message)
{
    struct biosal_tip_manager *concrete_self;
    concrete_self = thorium_actor_concrete_actor(self);

    core_vector_unpack(&concrete_self->spawners,
                    BUFFER(message));

    LOG("got spawner list");

    LOG("Tell %d ACTION_START_REPLY",
                        concrete_self->__supervisor);

    TELL(1, concrete_self->__supervisor,
                        ACTION_START_REPLY,
                        TYPE_INT, ACTION_START_REPLY);
}
