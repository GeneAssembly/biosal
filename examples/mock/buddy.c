
#include "buddy.h"

#include <stdio.h>

void buddy_init(struct thorium_actor *self);
void buddy_destroy(struct thorium_actor *self);
void buddy_receive(struct thorium_actor *self, struct thorium_message *message);

/* this script is required */
struct thorium_script buddy_script = {
    .identifier = SCRIPT_BUDDY,
    .init = buddy_init,
    .destroy = buddy_destroy,
    .receive = buddy_receive,
    .size = sizeof(struct buddy),
    .name = "buddy"
};

void buddy_init(struct thorium_actor *actor)
{
    struct buddy *buddy1;

    buddy1 = (struct buddy *)thorium_actor_concrete_actor(actor);
    buddy1->received = 0;
}

void buddy_destroy(struct thorium_actor *actor)
{
    struct buddy *buddy1;

    buddy1 = (struct buddy *)thorium_actor_concrete_actor(actor);
    buddy1->received = -1;
}

void buddy_receive(struct thorium_actor *actor, struct thorium_message *message)
{
    int tag;
    int source;
    int name;

    name = thorium_actor_name(actor);
    source = thorium_message_source(message);
    tag = thorium_message_action(message);

    if (tag == ACTION_BUDDY_BOOT) {

        printf("ACTION_BUDDY_BOOT\n");
        thorium_actor_print(actor);

        thorium_message_init(message, ACTION_BUDDY_BOOT_REPLY, 0, NULL);
        thorium_actor_send(actor, source, message);

    } else if (tag == ACTION_BUDDY_HELLO) {

        printf("ACTION_BUDDY_HELLO\n");

        /* pin the actor to the worker for no reason !
         */

        /*
        thorium_actor_send_to_self_empty(actor, ACTION_PIN_TO_WORKER);
        */

        thorium_message_init(message, ACTION_BUDDY_HELLO_REPLY, 0, NULL);
        thorium_actor_send(actor, source, message);

    } else if (tag == ACTION_ASK_TO_STOP) {

        printf("BUDDY_DIE\n");

        printf("buddy_receive Actor %i received a message (%i BUDDY_DIE) from actor %i\n",
                        name, tag, source);

        /*
        thorium_actor_send_to_self_empty(actor, ACTION_UNPIN_FROM_WORKER);
        */

        thorium_message_init(message, ACTION_STOP, 0, NULL);
        thorium_actor_send(actor, name, message);
    }
}
