
#ifndef _BSAL_INPUT_STREAM_H
#define _BSAL_INPUT_STREAM_H

#include "input_proxy.h"

#include <engine/actor.h>

#define BSAL_INPUT_STREAM_SCRIPT 0xeb2fe16a

struct bsal_input_stream {
    struct bsal_input_proxy proxy;
    int proxy_ready;
    char *buffer_for_sequence;
    int maximum_sequence_length;
    int open;
};

enum {
    BSAL_INPUT_OPEN = BSAL_TAG_OFFSET_INPUT_STREAM, /* +0 */
    BSAL_INPUT_OPEN_OK,
    BSAL_INPUT_ERROR,
    BSAL_INPUT_ERROR_FILE_NOT_FOUND,
    BSAL_INPUT_ERROR_FORMAT_NOT_SUPPORTED,
    BSAL_INPUT_ERROR_ALREADY_OPEN,
    BSAL_INPUT_ERROR_FILE_NOT_OPEN,
    BSAL_INPUT_COUNT,
    BSAL_INPUT_COUNT_YIELD,
    BSAL_INPUT_COUNT_PROGRESS, /* +9 */
    BSAL_INPUT_COUNT_READY,
    BSAL_INPUT_COUNT_RESULT,
    BSAL_INPUT_CLOSE,
    BSAL_INPUT_CLOSE_OK,
    BSAL_INPUT_GET_SEQUENCE,
    BSAL_INPUT_GET_SEQUENCE_END,
    BSAL_INPUT_GET_SEQUENCE_REPLY /* +16 */
};

extern struct bsal_script bsal_input_script;

void bsal_input_stream_init(struct bsal_actor *actor);
void bsal_input_stream_destroy(struct bsal_actor *actor);
void bsal_input_stream_receive(struct bsal_actor *actor, struct bsal_message *message);

int bsal_input_stream_has_error(struct bsal_actor *actor,
                struct bsal_message *message);

int bsal_input_stream_check_open_error(struct bsal_actor *actor,
                struct bsal_message *message);
#endif
