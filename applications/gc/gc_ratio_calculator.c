
#include "gc_ratio_calculator.h"

#include <stdio.h>

struct thorium_script gc_ratio_calculator_script = {
    .identifier = SCRIPT_GC_RATIO_CALCULATOR,
    .init = gc_ratio_calculator_init,
    .destroy = gc_ratio_calculator_destroy,
    .receive = gc_ratio_calculator_receive,
    .size = sizeof(struct gc_ratio_calculator),
    .name = "gc_ratio_calculator",
    .author = "Fangfang Xia",
    .version = "",
    .description = ""
};

void gc_ratio_calculator_init(struct thorium_actor *actor)
{
    struct gc_ratio_calculator *concrete_actor;
    concrete_actor = thorium_actor_concrete_actor(actor);
    bsal_vector_init(&concrete_actor->spawners, sizeof(int));
    concrete_actor->completed = 0;

    thorium_actor_add_action(actor, THORIUM_ACTOR_START, gc_ratio_calculator_start);
    thorium_actor_add_action(actor, GC_HELLO, gc_ratio_calculator_hello);
    thorium_actor_add_action(actor, GC_HELLO_REPLY, gc_ratio_calculator_hello_reply);
    thorium_actor_add_action(actor, THORIUM_ACTOR_NOTIFY, gc_ratio_calculator_notify);
    thorium_actor_add_action(actor, THORIUM_ACTOR_ASK_TO_STOP, gc_ratio_calculator_ask_to_stop);
}

void gc_ratio_calculator_destroy(struct thorium_actor *actor)
{
    struct gc_ratio_calculator *concrete_actor;
    concrete_actor = thorium_actor_concrete_actor(actor);
    bsal_vector_destroy(&concrete_actor->spawners);
}

void gc_ratio_calculator_receive(struct thorium_actor *actor, struct thorium_message *message)
{
    thorium_actor_use_route(actor, message);
}

/* dispatch handlers */

void gc_ratio_calculator_start(struct thorium_actor *actor, struct thorium_message *message)
{
    int name;
    int tag;
    int source;
    int index;
    int size;
    int neighbor_index;
    int neighbor_name;
    void * buffer;

    struct gc_ratio_calculator *concrete_actor;
    struct bsal_vector *spawners;

    concrete_actor = thorium_actor_concrete_actor(actor);

    name = thorium_actor_name(actor);
    tag = thorium_message_tag(message);
    buffer = thorium_message_buffer(message);
    source = thorium_message_source(message);
    spawners = &concrete_actor->spawners;
    size = bsal_vector_size(spawners);

    printf("received THORIUM_ACTOR_START\n");

    bsal_vector_unpack(spawners, buffer);
    size = bsal_vector_size(spawners);
    index = bsal_vector_index_of(spawners, &name);
    neighbor_index = (index + 1) % size;
    neighbor_name = bsal_vector_at_as_int(spawners, neighbor_index);

    printf("about to send to neighbor\n");
    thorium_actor_send_empty(actor, neighbor_name, GC_HELLO);
    /* thorium_message_init(&new_message, GC_HELLO, 0, NULL); */
    /* thorium_actor_send(actor, neighbor_name, &new_message); */
    /* thorium_message_destroy(&new_message); */
}

void gc_ratio_calculator_hello(struct thorium_actor *actor, struct thorium_message *message)
{
    printf("received GC_HELLO\n");

    thorium_actor_send_reply_empty(actor, GC_HELLO_REPLY);
}

void gc_ratio_calculator_hello_reply(struct thorium_actor *actor, struct thorium_message *message)
{
    struct thorium_message new_message;
    int name;
    int source;
    int boss;

    struct gc_ratio_calculator *concrete_actor;
    struct bsal_vector *spawners;

    concrete_actor = thorium_actor_concrete_actor(actor);

    name = thorium_actor_name(actor);
    source = thorium_message_source(message);
    spawners = &concrete_actor->spawners;

    printf("Actor %d is satisfied with a reply from the neighbor %d.\n", name, source);

    boss = bsal_vector_at_as_int(spawners, 0);
    thorium_message_init(&new_message, THORIUM_ACTOR_NOTIFY, 0, NULL);
    thorium_actor_send(actor, boss, &new_message);
    thorium_message_destroy(&new_message);
}

void gc_ratio_calculator_notify(struct thorium_actor *actor, struct thorium_message *message)
{
    int size;

    struct gc_ratio_calculator *concrete_actor;
    struct bsal_vector *spawners;

    concrete_actor = thorium_actor_concrete_actor(actor);

    spawners = &concrete_actor->spawners;
    size = bsal_vector_size(spawners);

    printf("received NOTIFY\n");

    ++concrete_actor->completed;
    if (concrete_actor->completed == size) {
        thorium_actor_send_range_empty(actor, spawners, THORIUM_ACTOR_ASK_TO_STOP);
    }
}

void gc_ratio_calculator_ask_to_stop(struct thorium_actor *actor, struct thorium_message *message)
{
    printf("received ASK_TO_STOP\n");
    thorium_actor_send_to_self_empty(actor, THORIUM_ACTOR_STOP);
}



