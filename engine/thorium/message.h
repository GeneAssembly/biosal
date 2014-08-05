
#ifndef BSAL_MESSAGE_H
#define BSAL_MESSAGE_H

/* helpers */

#include "core/helpers/message_helper.h"


/*
 * This is a message.
 *
 * tags have a value from 0 to MPI_TAB_UB.
 * MPI_TAB_UB is at least 32767 (MPI 1.3, 2.0, 2.1, 2.2, 3.0)
 */
struct bsal_message {
    void *buffer;
    int count;
    int tag;

    int source_actor;
    int destination_actor;

    int source_node;
    int destination_node;

    int routing_source;
    int routing_destination;
    int worker;
    int is_recycled;
};

void bsal_message_init(struct bsal_message *self, int tag, int count, void *buffer);
void bsal_message_init_copy(struct bsal_message *self, struct bsal_message *old_message);
void bsal_message_destroy(struct bsal_message *self);

int bsal_message_source(struct bsal_message *self);
void bsal_message_set_source(struct bsal_message *self, int source);

int bsal_message_destination(struct bsal_message *self);
void bsal_message_set_destination(struct bsal_message *self, int destination);

int bsal_message_tag(struct bsal_message *self);
void bsal_message_set_tag(struct bsal_message *self, int tag);

void *bsal_message_buffer(struct bsal_message *self);
void bsal_message_set_buffer(struct bsal_message *self, void *buffer);

int bsal_message_count(struct bsal_message *self);
void bsal_message_set_count(struct bsal_message *self, int count);

int bsal_message_source_node(struct bsal_message *self);
void bsal_message_set_source_node(struct bsal_message *self, int source);

int bsal_message_destination_node(struct bsal_message *self);
void bsal_message_set_destination_node(struct bsal_message *self, int destination);

int bsal_message_metadata_size(struct bsal_message *self);
void bsal_message_read_metadata(struct bsal_message *self);
void bsal_message_write_metadata(struct bsal_message *self);

void bsal_message_print(struct bsal_message *self);

void bsal_message_set_worker(struct bsal_message *self, int worker);
int bsal_message_get_worker(struct bsal_message *self);

int bsal_message_is_recycled(struct bsal_message *self);
void bsal_message_recycle(struct bsal_message *self);

#endif
