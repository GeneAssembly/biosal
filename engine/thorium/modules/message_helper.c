
#include "message_helper.h"

#include <engine/thorium/message.h>

#include <core/system/debugger.h>

#include <stdint.h>

int thorium_message_unpack_int(struct thorium_message *message, int offset, int *value)
{
    int bytes;
    void *buffer;
    int *pointer;

    CORE_DEBUGGER_ASSERT(offset < thorium_message_count(message));
    CORE_DEBUGGER_ASSERT((int)(offset + sizeof(int)) <= thorium_message_count(message));

    if (offset >= thorium_message_count(message)) {
        return -1;
    }

    bytes = sizeof(int);
    buffer = thorium_message_buffer(message);

    pointer = (int *)((char *)buffer + offset);

    *value = *pointer;

    return bytes;
}

int thorium_message_unpack_uint64_t(struct thorium_message *message, int offset, uint64_t *value)
{
    int bytes;
    void *buffer;
    uint64_t *pointer;

    if (offset >= thorium_message_count(message)) {
        return -1;
    }

    bytes = sizeof(uint64_t);
    buffer = thorium_message_buffer(message);

    pointer = (uint64_t *)((char *)buffer + offset);

    *value = *pointer;

    return bytes;
}

void thorium_message_get_all(struct thorium_message *message, int *tag, int *count, void **buffer, int *source)
{
    *tag = thorium_message_action(message);
    *count = thorium_message_count(message);
    *buffer = thorium_message_buffer(message);
    *source = thorium_message_source(message);
}

int thorium_message_unpack_int64_t(struct thorium_message *message, int offset, int64_t *value)
{
    int bytes;
    void *buffer;
    int64_t *pointer;

    if (offset >= thorium_message_count(message)) {
        return -1;
    }

    bytes = sizeof(int64_t);
    buffer = thorium_message_buffer(message);

    pointer = (int64_t *)((char *)buffer + offset);

    *value = *pointer;

    return bytes;
}

int thorium_message_unpack_double(struct thorium_message *message, int offset, double *value)
{
    int bytes;
    void *buffer;
    double *pointer;

    if (offset >= thorium_message_count(message)) {
        return -1;
    }

    bytes = sizeof(double);
    buffer = thorium_message_buffer(message);

    pointer = (double *)((char *)buffer + offset);

    *value = *pointer;

    return bytes;
}

int thorium_message_unpack_2_int(struct thorium_message *message, int *value1, int *value2)
{
    int offset;
    int count;
    int required;

    count = thorium_message_count(message);

    required = sizeof(*value1) + sizeof(*value2);

    if (required > count) {
        return -1;
    }

    offset = 0;
    offset += thorium_message_unpack_int(message, offset, value1);
    offset += thorium_message_unpack_int(message, offset, value2);

    return offset;
}
