
#ifndef _SENDER_H
#define _SENDER_H

#include <biosal.h>

#define SENDER_SCRIPT 0x99a9f4e1

struct sender {
    int next;
};

#define SENDER_HELLO 0x000050c1
#define SENDER_HELLO_REPLY 0x00001716
#define SENDER_KILL 0x00007cd7
#define SENDER_SET_NEXT 0x00000f5d
#define SENDER_SET_NEXT_REPLY 0x000075c6

extern struct bsal_script sender_script;

void sender_init(struct bsal_actor *actor);
void sender_destroy(struct bsal_actor *actor);
void sender_receive(struct bsal_actor *actor, struct bsal_message *message);

#endif
