
#include "sender.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct bsal_script sender_script = {
    .name = SENDER_SCRIPT,
    .init = sender_init,
    .destroy = sender_destroy,
    .receive = sender_receive,
    .size = sizeof(struct sender)
};

void sender_init(struct bsal_actor *actor)
{
    struct sender *sender1;
/*
    int name;

    name = bsal_actor_name(actor);
    */
    sender1 = (struct sender *)bsal_actor_concrete_actor(actor);
    sender1->received = 0;
    sender1->actors_per_node = 1000;

}

void sender_destroy(struct bsal_actor *actor)
{
    struct sender *sender1;

    /*
    printf("DEBUG sender_destroy\n");
    */
    sender1 = (struct sender *)bsal_actor_concrete_actor(actor);
    sender1->received = 0;
    sender1->actors_per_node = 0;

}

void sender_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int name;
    int source;

    tag = bsal_message_tag(message);
    source = bsal_message_source(message);
    name = bsal_actor_name(actor);

    if (tag == BSAL_ACTOR_START) {

        sender_start(actor, message);

    } else if (tag == SENDER_HELLO) {

        sender_hello(actor, message);

    } else if (tag == SENDER_KILL) {
        /*printf("Receives SENDER_KILL\n"); */

        bsal_message_init(message, BSAL_ACTOR_STOP, 0, NULL);
        bsal_actor_send(actor, name, message);

    } else if (tag == BSAL_ACTOR_SYNCHRONIZE) {

        bsal_message_init(message, BSAL_ACTOR_SYNCHRONIZE_REPLY, 0, NULL);
        bsal_actor_send(actor, source, message);

    } else if (tag == BSAL_ACTOR_SYNCHRONIZED) {

        /*printf("sender_receive BSAL_ACTOR_SYNCHRONIZE_REPLY\n");*/

        printf("Completed synchronize !\n");
        sender_kill_all(actor, message);
    }
}

void sender_kill_all(struct bsal_actor *actor, struct bsal_message *message)
{
    int total;
    int size;
    struct sender *sender1;
    sender1 = (struct sender *)bsal_actor_concrete_actor(actor);

    size = bsal_actor_nodes(actor);
    total = size * sender1->actors_per_node;

    bsal_message_init(message, SENDER_KILL, 0, NULL);

    printf("Killing range %i to %i\n",
                    0, total - 1);

   /* the default binomial-tree algorithm can not
    * be used here because proxy actors may die
    * before they are needed.
    */
    bsal_actor_send_range_standard(actor, 0, total - 1, message);
}

void sender_hello(struct bsal_actor *actor, struct bsal_message *message)
{
    int next;
    int total;
    int size;
    int name;
    struct sender *sender1;
    int events;

    /*printf("sender_hello\n"); */

    sender1 = (struct sender *)bsal_actor_concrete_actor(actor);
    name = bsal_actor_name(actor);
    size = bsal_actor_nodes(actor);
    /*
    printf("DEBUG actor %i size: %i, actors_per_node: %i\n", name, size,
                    sender1->actors_per_node);
                    */
    total = size * sender1->actors_per_node;

    memcpy(&events, bsal_message_buffer(message), sizeof(events));
    events--;

    if (events == 0) {
        printf("sender_hello completed test on actor %i, %i actors in total\n",
                        name, total);

        printf("synchronize %i-%i\n", 0, total - 1);
        bsal_actor_synchronize(actor, 0, total - 1);
        /*sender_kill_all(actor, message); */
    } else {

        memcpy(bsal_message_buffer(message), &events, sizeof(events));
        next = (name + 1) % total;

        if (events % 20000 == 0) {
            printf("sender_hello remaining events %i\n", events);
        }

        /*printf("sender_hello send SENDER_HELLO\n");*/
        bsal_actor_send(actor, next, message);
    }
}

void sender_start(struct bsal_actor *actor, struct bsal_message *message)
{
    int name;
    int size;
    int i;
    int events;
    int total;
    int next;
    struct sender *sender1;
    int argc;
    char **argv;
    int message_size;

    printf("sender_start\n");

    sender1 = (struct sender *)bsal_actor_concrete_actor(actor);


    name = bsal_actor_name(actor);
    size = bsal_actor_nodes(actor);
    total = size * sender1->actors_per_node;

    /* spawn a lot of actors ! */

    for (i = 0; i < sender1->actors_per_node - 1; i++) {
        bsal_actor_spawn(actor, SENDER_SCRIPT);
    }

    if (name != 0) {
        return;
    }

    /* \see http://rlrr.drum-corps.net/misc/primes3.shtml
     */
    events = 200087;
    message_size = sizeof(events);

    argc = bsal_actor_argc(actor);
    argv = bsal_actor_argv(actor);

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-messages") == 0 && i + 1 < argc) {
            events = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-buffer_count") == 0 && i + 1 < argc) {
            message_size = atoi(argv[i + 1]);
        }
    }

    if (message_size < sizeof(events)) {
        message_size = sizeof(events);
    }

    printf("Using %d messages (-messages)\n", events);
    printf("Using %d bytes for buffer_count (-buffer_count)\n", message_size);

    /*events = 10000;*/

    printf("sender_start send SENDER_HELLO, system has: %i bsal_actors on %i "
                    "bsal_nodes (%i worker threads each)\n",
                    sender1->actors_per_node * bsal_actor_nodes(actor),
                    bsal_actor_nodes(actor),
                    bsal_actor_threads(actor));

    bsal_message_init(message, SENDER_HELLO, message_size, &events);

    next = (name + 1) % total;
    bsal_actor_send(actor, next, message);
}
