
#include "table.h"

#include <stdio.h>

struct bsal_actor_vtable table_script = {
    .name = TABLE_SCRIPT,
    .init = table_init,
    .destroy = table_destroy,
    .receive = table_receive,
    .size = sizeof(struct table)
};

void table_init(struct bsal_actor *actor)
{
    struct table *table1;

    table1 = (struct table *)bsal_actor_pointer(actor);
    table1->done = 0;
}

void table_destroy(struct bsal_actor *actor)
{
    printf("actor %d dies\n", bsal_actor_name(actor));
}

void table_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    int name;
    int nodes;
    int remote;
    struct bsal_message spawn_message;
    int script;
    int new_actor;
    void *buffer;
    struct table *table1;

    nodes = bsal_actor_nodes(actor);
    table1 = (struct table *)bsal_actor_pointer(actor);
    source = bsal_message_source(message);
    tag = bsal_message_tag(message);
    name = bsal_actor_name(actor);

    if (tag == BSAL_ACTOR_START) {
        printf("Actor %i receives BSAL_ACTOR_START from actor %i\n",
                        name,  source);

        remote = name + 1;
        remote %= nodes;

        script = TABLE_SCRIPT;
        bsal_message_init(&spawn_message, BSAL_ACTOR_SPAWN, sizeof(script), &script);
        bsal_actor_send(actor, remote, &spawn_message);

        /*
        printf("sending notification\n");
        bsal_message_set_tag(message, TABLE_NOTIFY);
        bsal_actor_send(actor, 0, message);
*/
        /*
        bsal_actor_die(actor);
        */
    } else if (tag == BSAL_ACTOR_SPAWN_REPLY) {

        buffer = bsal_message_buffer(message);
        new_actor= *(int *)buffer;

        printf("Actor %i receives BSAL_ACTOR_SPAWN_REPLY from actor %i,"
                        " new actor is %d\n",
                        name,  source, new_actor);

        bsal_message_set_tag(message, TABLE_DIE2);
        bsal_actor_send(actor, new_actor, message);

        bsal_message_set_tag(message, TABLE_NOTIFY);
        bsal_actor_send(actor, 0, message);

    } else if (tag == TABLE_DIE2) {

        printf("Actor %i receives TABLE_DIE2 from actor %i\n",
                        name,  source);

        if (name < nodes) {
            return;
        }

        bsal_actor_die(actor);

    } else if (tag == TABLE_DIE) {

        printf("Actor %i receives TABLE_DIE from actor %i\n",
                        name,  source);

        bsal_actor_die(actor);

    } else if (tag == TABLE_NOTIFY) {

        printf("Actor %i receives TABLE_NOTIFY from actor %i\n",
                        name,  source);

        table1->done++;

        if (table1->done == nodes) {
            printf("actor %d kills %d to %d\n",
                           name, 0, nodes - 1);
            bsal_message_set_tag(message, TABLE_DIE);
            bsal_actor_send_range_standard(actor, 0, nodes - 1, message);
        }
    }
}
