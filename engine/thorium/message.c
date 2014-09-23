
#include "message.h"

#include <core/system/packer.h>
#include <core/system/debugger.h>

#include <stdlib.h>
#include <string.h>

void thorium_message_init(struct thorium_message *message, int action, int count,
                void *buffer)
{
    message->action= action;
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

    thorium_message_set_type(message,  THORIUM_MESSAGE_TYPE_NONE);
}

void thorium_message_destroy(struct thorium_message *message)
{
    message->source_actor = -1;
    message->destination_actor = -1;
    message->action= -1;
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

int thorium_message_action(struct thorium_message *message)
{
    return message->action;
}

void thorium_message_set_source(struct thorium_message *message, int source)
{
    message->source_actor = source;
}

void thorium_message_set_destination(struct thorium_message *message, int destination)
{
    message->destination_actor = destination;
}

void thorium_message_print(struct thorium_message *self)
{
    printf("Message Tag %d Count %d SourceActor %d DestinationActor %d\n",
                    self->action,
                    self->count,
                    self->source_actor,
                    self->destination_actor);

    bsal_debugger_examine(self->buffer, self->count);
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

void thorium_message_set_action(struct thorium_message *message, int action)
{
    message->action= action;
}

int thorium_message_metadata_size(struct thorium_message *self)
{
    return thorium_message_pack_unpack(self, BSAL_PACKER_OPERATION_PACK_SIZE,
                    NULL);
}

int thorium_message_write_metadata(struct thorium_message *self)
{
    return thorium_message_pack_unpack(self, BSAL_PACKER_OPERATION_PACK,
                    (char *)self->buffer + self->count);
}

int thorium_message_read_metadata(struct thorium_message *self)
{
    return thorium_message_pack_unpack(self, BSAL_PACKER_OPERATION_UNPACK,
                    (char *)self->buffer + self->count);
}

void thorium_message_set_count(struct thorium_message *message, int count)
{
    message->count = count;
}

void thorium_message_init_copy(struct thorium_message *message, struct thorium_message *old_message)
{
    thorium_message_init(message,
                    thorium_message_action(old_message),
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

    thorium_message_set_type(message, THORIUM_MESSAGE_TYPE_WORKER_OUTBOUND);
}

int thorium_message_worker(struct thorium_message *message)
{
    return message->worker;
}

void thorium_message_init_with_nodes(struct thorium_message *self, int count, void *buffer, int source,
                int destination)
{
    int action;

    action = -1;

    thorium_message_init(self, action, count, buffer);

    /*
     * Initially assign the MPI source rank and MPI destination
     * rank for the actor source and actor destination, respectively.
     * Then, read the metadata and resolve the MPI rank from
     * that. The resolved MPI ranks should be the same in all cases
     */

    thorium_message_set_source(self, source);
    thorium_message_set_destination(self, destination);

    thorium_message_set_source_node(self, source);
    thorium_message_set_destination_node(self, destination);

    thorium_message_set_type(self, THORIUM_MESSAGE_TYPE_NODE_INBOUND);
}

int thorium_message_pack_unpack(struct thorium_message *self, int operation, void *buffer)
{
    struct bsal_packer packer;
    int count;

    bsal_packer_init(&packer, operation, buffer);

    bsal_packer_process(&packer, &self->source_actor, sizeof(self->source_actor));
    bsal_packer_process(&packer, &self->destination_actor, sizeof(self->destination_actor));
    bsal_packer_process(&packer, &self->action, sizeof(self->action));

    count = bsal_packer_get_byte_count(&packer);

    bsal_packer_destroy(&packer);

    return count;
}

int thorium_message_type(struct thorium_message *self)
{
    return self->type;
}

void thorium_message_set_type(struct thorium_message *self, int type)
{
    self->type = type;
}
