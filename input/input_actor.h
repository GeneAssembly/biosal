
#ifndef _BSAL_INPUT_ACTOR_H
#define _BSAL_INPUT_ACTOR_H

#include "input_proxy.h"

#include <engine/actor.h>

struct bsal_input_actor {
    struct bsal_input_proxy proxy;
    char *file_name;
    int proxy_ready;
};

enum {
    BSAL_INPUT_ACTOR_OPEN = BSAL_TAG_OFFSET_INPUT_ACTOR,
    BSAL_INPUT_ACTOR_OPEN_OK,
    BSAL_INPUT_ACTOR_OPEN_NOT_FOUND,
    BSAL_INPUT_ACTOR_ERROR,
    BSAL_INPUT_ACTOR_COUNT,
    BSAL_INPUT_ACTOR_COUNT_YIELD,
    BSAL_INPUT_ACTOR_COUNT_CONTINUE,
    BSAL_INPUT_ACTOR_COUNT_READY,
    BSAL_INPUT_ACTOR_COUNT_RESULT,
    BSAL_INPUT_ACTOR_CLOSE
};

struct bsal_actor_vtable bsal_input_actor_vtable;

void bsal_input_actor_init(struct bsal_actor *actor);
void bsal_input_actor_destroy(struct bsal_actor *actor);
void bsal_input_actor_receive(struct bsal_actor *actor, struct bsal_message *message);

int bsal_input_actor_has_error(struct bsal_actor *actor,
                struct bsal_message *message);

#endif
