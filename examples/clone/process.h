
#ifndef _PROCESS_H
#define _PROCESS_H

#include <biosal.h>

#define PROCESS_SCRIPT 0x083212f2

struct process {
    int clone;
    struct bsal_vector initial_processes;
    int value;
    int ready;
    int cloned;
};

extern struct thorium_script process_script;

void process_init(struct thorium_actor *self);
void process_destroy(struct thorium_actor *self);
void process_receive(struct thorium_actor *self, struct thorium_message *message);

#endif
