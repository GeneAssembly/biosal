
#ifndef _SENDER_H
#define _SENDER_H

#include <biosal.h>

struct reader {
    int foo;
};

struct bsal_actor_vtable reader_vtable;

void reader_init(struct bsal_actor *actor);
void reader_destroy(struct bsal_actor *actor);
void reader_receive(struct bsal_actor *actor, struct bsal_message *message);

#endif
