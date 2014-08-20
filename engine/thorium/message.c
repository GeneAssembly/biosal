
#include "message.h"

#include <stdlib.h>
#include <string.h>

void thorium_message_init(struct thorium_message *message, int tag, int count,
                void *buffer)
{
    message->tag = tag;
    message->buffer = buffer;
    message->count = count;

    message->source_actor = -1;
    message->destination_actor = -1;

    /* ranks are set with thorium_node_resolve */
    message->source_node = -1;
    message->destination_node = -1;

    message->routing_source = -1;
    message->routing_destination = -1;

    message->worker = -1;
}

void thorium_message_destroy(struct thorium_message *message)
{
    message->source_actor = -1;
    message->destination_actor = -1;
    message->tag = -1;
    message->buffer = NULL;
    message->count= 0;
}

int thorium_message_source(struct thorium_message *message)
{
    return message->source_actor;
}

int thorium_message_destination(struct thorium_message *message)
{
    return message->destination_actor;
}

int thorium_message_source_node(struct thorium_message *message)
{
    return message->source_node;
}

int thorium_message_destination_node(struct thorium_message *message)
{
    return message->destination_node;
}

int thorium_message_tag(struct thorium_message *message)
{
    return message->tag;
}

void thorium_message_set_source(struct thorium_message *message, int source)
{
    message->source_actor = source;
}

void thorium_message_set_destination(struct thorium_message *message, int destination)
{
    message->destination_actor = destination;
}

void thorium_message_print(struct thorium_message *message)
{
}

void *thorium_message_buffer(struct thorium_message *message)
{
    return message->buffer;
}

int thorium_message_count(struct thorium_message *message)
{
    return message->count;
}

void thorium_message_set_source_node(struct thorium_message *message, int source)
{
    message->source_node = source;
}

void thorium_message_set_destination_node(struct thorium_message *message, int destination)
{
    message->destination_node = destination;
}

void thorium_message_set_buffer(struct thorium_message *message, void *buffer)
{
    message->buffer = buffer;
}

void thorium_message_set_tag(struct thorium_message *message, int tag)
{
    message->tag = tag;
}

int thorium_message_metadata_size(struct thorium_message *message)
{
    int total;

    total = 0;

    total += sizeof(message->source_actor);
    total += sizeof(message->destination_actor);
    total += sizeof(message->tag);

    return total;
}

void thorium_message_write_metadata(struct thorium_message *message)
{
    /* This could be a single memcpy with N *sizeof(int)
     * because source_actor and destination_actor are consecutive
     */
    int offset;
    int size;

    offset = message->count;

    size = sizeof(message->source_actor);
    memcpy((char *)message->buffer + offset, &message->source_actor, size);
    offset += size;

    size = sizeof(message->destination_actor);
    memcpy((char *)message->buffer + offset, &message->destination_actor, size);
    offset += size;

    size = sizeof(message->tag);
    memcpy((char *)message->buffer + offset, &message->tag, size);
    offset += size;
}

void thorium_message_read_metadata(struct thorium_message *message)
{
    /* TODO this could be a single memcpy with 2 *sizeof(int)
     * because source_actor and destination_actor are consecutive
     */
    int offset;
    int size;

    offset = message->count;

    size = sizeof(message->source_actor);
    memcpy(&message->source_actor, (char *)message->buffer + offset, size);
    offset += size;

    size = sizeof(message->destination_actor);
    memcpy(&message->destination_actor, (char *)message->buffer + offset, size);
    offset += size;

    size = sizeof(message->tag);
    memcpy(&message->tag, (char *)message->buffer + offset, size);
    offset += size;
}

void thorium_message_set_count(struct thorium_message *message, int count)
{
    message->count = count;
}

void thorium_message_init_copy(struct thorium_message *message, struct thorium_message *old_message)
{
    thorium_message_init(message,
                    thorium_message_tag(old_message),
                    thorium_message_count(old_message),
                    thorium_message_buffer(old_message));

    thorium_message_set_source(message,
                    thorium_message_source(old_message));
    thorium_message_set_destination(message,
                    thorium_message_destination(old_message));
}

void thorium_message_set_worker(struct thorium_message *message, int worker)
{
    message->worker = worker;
}

int thorium_message_get_worker(struct thorium_message *message)
{
    return message->worker;
}

void thorium_message_init_with_nodes(struct thorium_message *self, int tag, int count, void *buffer, int source,
                int destination)
{
    thorium_message_init(self, tag, count, buffer);

    /*
     * Initially assign the MPI source rank and MPI destination
     * rank for the actor source and actor destination, respectively.
     * Then, read the metadata and resolve the MPI rank from
     * that. The resolved MPI ranks should be the same in all cases
     */

    thorium_message_set_source(self, source);
    thorium_message_set_destination(self, destination);
}
