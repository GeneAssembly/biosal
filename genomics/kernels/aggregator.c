
#include "aggregator.h"

#include <genomics/data/dna_kmer_block.h>
#include <genomics/data/dna_kmer.h>

#include <genomics/storage/sequence_store.h>
#include <genomics/kernels/dna_kmer_counter_kernel.h>
#include <genomics/storage/kmer_store.h>

#include <core/helpers/actor_helper.h>
#include <core/helpers/message_helper.h>
#include <core/helpers/vector_helper.h>

#include <core/system/packer.h>

#include <core/structures/vector_iterator.h>

#include <core/system/debugger.h>
#include <core/system/memory.h>

#include <stdio.h>
#include <inttypes.h>

/* debugging options
 */
/*
#define BSAL_AGGREGATOR_DEBUG
*/

/* aggregator runtime constants
 */

#define BSAL_FALSE 0
#define BSAL_TRUE 1

/*
 * Disable memory tracking for increased
 * performance
 */
#define BSAL_AGGREGATOR_DISABLE_TRACKING

/*
#define BSAL_AGGREGATOR_DEBUG_FLUSHING
*/

struct bsal_script bsal_aggregator_script = {
    .name = BSAL_AGGREGATOR_SCRIPT,
    .init = bsal_aggregator_init,
    .destroy = bsal_aggregator_destroy,
    .receive = bsal_aggregator_receive,
    .size = sizeof(struct bsal_aggregator),
    .description = "aggregator"
};

void bsal_aggregator_init(struct bsal_actor *self)
{
    struct bsal_aggregator *concrete_actor;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);
    concrete_actor->received = 0;
    concrete_actor->last = 0;

    bsal_vector_init(&concrete_actor->customers, sizeof(int));

    concrete_actor->flushed = 0;

    bsal_dna_codec_init(&concrete_actor->codec);

    bsal_ring_queue_init(&concrete_actor->stalled_producers, sizeof(int));
    concrete_actor->active_messages = 0;

    concrete_actor->forced = BSAL_FALSE;

    bsal_actor_register(self, BSAL_AGGREGATE_KERNEL_OUTPUT,
                    bsal_aggregator_aggregate_kernel_output);

    /* Enable cloning stuff
     */
    bsal_actor_helper_send_to_self_empty(self, BSAL_ACTOR_PACK_ENABLE);

    bsal_actor_register(self, BSAL_ACTOR_PACK,
                    bsal_aggregator_pack_message);
    bsal_actor_register(self, BSAL_ACTOR_UNPACK,
                    bsal_aggregator_unpack_message);

    printf("aggregator %d is online\n", bsal_actor_get_name(self));
}

void bsal_aggregator_destroy(struct bsal_actor *self)
{
    struct bsal_aggregator *concrete_actor;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);
    concrete_actor->received = 0;
    concrete_actor->last = 0;
    bsal_dna_codec_destroy(&concrete_actor->codec);

    bsal_ring_queue_destroy(&concrete_actor->stalled_producers);

    bsal_vector_destroy(&concrete_actor->customers);

}

void bsal_aggregator_receive(struct bsal_actor *self, struct bsal_message *message)
{
    int tag;
    struct bsal_aggregator *concrete_actor;
    void *buffer;
    /*int source;*/

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);
    buffer = bsal_message_buffer(message);
    tag = bsal_message_tag(message);
    /*source = bsal_message_source(message);*/

    if (tag == BSAL_ACTOR_ASK_TO_STOP) {

        bsal_actor_helper_ask_to_stop(self, message);

    } else if (tag == BSAL_SET_KMER_LENGTH) {

        bsal_message_helper_unpack_int(message, 0, &concrete_actor->kmer_length);

        bsal_actor_helper_send_reply_empty(self, BSAL_SET_KMER_LENGTH_REPLY);

    } else if (tag == BSAL_ACTOR_SET_CONSUMERS) {

        bsal_aggregator_set_consumers(self, buffer);

        bsal_actor_helper_send_reply_empty(self, BSAL_ACTOR_SET_CONSUMERS_REPLY);

    } else if (tag == BSAL_AGGREGATOR_FLUSH) {


        concrete_actor->forced = BSAL_TRUE;

        bsal_actor_helper_send_reply_empty(self, BSAL_AGGREGATOR_FLUSH_REPLY);

    } else if (tag == BSAL_PUSH_KMER_BLOCK_REPLY) {

#ifdef BSAL_AGGREGATOR_DEBUG
        printf("BEFORE BSAL_PUSH_KMER_BLOCK_REPLY %d\n", concrete_actor->active_messages);
#endif

        concrete_actor->active_messages--;

        if (!concrete_actor->forced) {
            bsal_aggregator_verify(self, message);
        }
    }
}

void bsal_aggregator_flush(struct bsal_actor *self, int customer_index, struct bsal_vector *buffers,
                int force)
{
    struct bsal_dna_kmer_block *customer_block_pointer;
    struct bsal_aggregator *concrete_actor;
    int count;
    void *buffer;
    struct bsal_message message;
    int customer;
    struct bsal_memory_pool *ephemeral_memory;
    int threshold;

    /*
     * Only flush when required
     */
    threshold = BSAL_SEQUENCE_STORE_FINAL_BLOCK_SIZE * 3;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);

    ephemeral_memory = bsal_actor_get_ephemeral_memory(self);
    customer = bsal_actor_helper_get_acquaintance(self, &concrete_actor->customers, customer_index);
    customer_block_pointer = (struct bsal_dna_kmer_block *)bsal_vector_at(buffers, customer_index);
    count = bsal_dna_kmer_block_pack_size(customer_block_pointer,
                    &concrete_actor->codec);

    /*
     * Don't flush if the force parameter is not set and there are not enough
     * bytes.
     */
    if (!force && count < threshold) {
        return;

    }


#ifdef BSAL_AGGREGATOR_DEBUG_FLUSHING
    printf("DEBUG bsal_aggregator_flush actual %d threshold %d\n", count,
                    threshold);
#endif

    buffer = bsal_memory_pool_allocate(ephemeral_memory, count);
    bsal_dna_kmer_block_pack(customer_block_pointer, buffer,
                    &concrete_actor->codec);

    bsal_message_init(&message, BSAL_PUSH_KMER_BLOCK, count, buffer);
    bsal_actor_send(self, customer, &message);

    concrete_actor->active_messages++;

    bsal_message_destroy(&message);
    bsal_memory_pool_free(ephemeral_memory, buffer);

    buffer = NULL;

    if (concrete_actor->flushed % 50000 == 0) {
        printf("aggregator %d flushed %d blocks so far\n",
                        bsal_actor_get_name(self), concrete_actor->flushed);
    }

    /* Reset the buffer
     */

    bsal_dna_kmer_block_destroy(customer_block_pointer, ephemeral_memory);

    bsal_dna_kmer_block_init(customer_block_pointer, concrete_actor->kmer_length, -1,
                        concrete_actor->customer_block_size);

    concrete_actor->flushed++;
}

void bsal_aggregator_verify(struct bsal_actor *self, struct bsal_message *message)
{
    struct bsal_aggregator *concrete_actor;
    int producer_index;
    int producer;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);

    /* Only continue if there are not too many
     * active messages.
     */
    if (concrete_actor->active_messages <= concrete_actor->maximum_active_messages) {

        while (bsal_ring_queue_dequeue(&concrete_actor->stalled_producers, &producer_index)) {
            /* tell the producer to continue...
             */
            producer = bsal_actor_get_acquaintance(self, producer_index);
            bsal_actor_helper_send_empty(self, producer, BSAL_AGGREGATE_KERNEL_OUTPUT_REPLY);
        }
    }
}

void bsal_aggregator_aggregate_kernel_output(struct bsal_actor *self, struct bsal_message *message)
{
    int i;
    struct bsal_aggregator *concrete_actor;
    struct bsal_vector buffers;
    int producer_index;
    int customer_count;
    struct bsal_dna_kmer_block *customer_block_pointer;
    int entries;
    struct bsal_dna_kmer_block input_block;
    struct bsal_dna_kmer_block *output_block;
    struct bsal_vector *kmers;
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_dna_kmer *kmer;
    int source;
    void *buffer;
    int customer_index;
    struct bsal_vector_iterator iterator;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(self);

    if (bsal_vector_size(&concrete_actor->customers) == 0) {
        printf("Error: aggregator %d has no configured buffers\n",
                        bsal_actor_get_name(self));
        return;
    }

    ephemeral_memory = bsal_actor_get_ephemeral_memory(self);
    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);

    bsal_vector_init(&buffers, sizeof(struct bsal_dna_kmer_block));

    bsal_vector_resize(&buffers,
                    bsal_vector_size(&concrete_actor->customers));



    /* enqueue the producer
     */
    producer_index = bsal_actor_add_acquaintance(self, source);
    bsal_ring_queue_enqueue(&concrete_actor->stalled_producers, &producer_index);


#ifdef BSAL_AGGREGATOR_DEBUG
    BSAL_DEBUG_MARKER("aggregator receives");
    printf("name %d source %d UNPACK ON %d bytes\n",
                        bsal_actor_get_name(self), source, bsal_message_count(message));
#endif

    concrete_actor->received++;

    bsal_dna_kmer_block_unpack(&input_block, buffer, bsal_actor_get_ephemeral_memory(self),
                        &concrete_actor->codec);

#ifdef BSAL_AGGREGATOR_DEBUG
        BSAL_DEBUG_MARKER("aggregator before loop");
#endif

    /*
     * classify the kmers according to their ownership
     */

    kmers = bsal_dna_kmer_block_kmers(&input_block);
    entries = bsal_vector_size(kmers);

    customer_count = bsal_vector_size(&concrete_actor->customers);

    concrete_actor->customer_block_size = (entries / customer_count) * 2;

    /*
     * Reserve entries
     */

    for (i = 0; i < bsal_vector_size(&concrete_actor->customers); i++) {

        customer_block_pointer = (struct bsal_dna_kmer_block *)bsal_vector_at(&buffers,
                        i);
        bsal_dna_kmer_block_init(customer_block_pointer, concrete_actor->kmer_length, -1,
                        concrete_actor->customer_block_size);

    }


    for (i = 0; i < entries; i++) {
        kmer = (struct bsal_dna_kmer *)bsal_vector_at(kmers, i);

        /*
        bsal_dna_kmer_print(kmer);
        bsal_dna_kmer_length(kmer);
        */

        customer_index = bsal_dna_kmer_store_index(kmer, customer_count, concrete_actor->kmer_length,
                        &concrete_actor->codec, bsal_actor_get_ephemeral_memory(self));

        customer_block_pointer = (struct bsal_dna_kmer_block *)bsal_vector_at(&buffers,
                        customer_index);

#ifdef BSAL_AGGREGATOR_DEBUG
        printf("DEBUG customer_index %d block pointer %p\n",
                        customer_index, (void *)customer_block_pointer);

        BSAL_DEBUG_MARKER("aggregator before add");
#endif

        /*
         * add kmer to buffer and try to flush
         */

        /* classify the kmer and put it in the good buffer.
         */
        bsal_dna_kmer_block_add_kmer(customer_block_pointer, kmer, ephemeral_memory,
                        &concrete_actor->codec);

        /* Flush if necessary to avoid having very big buffers
         */
        if (i % 32 == 0) {
            bsal_aggregator_flush(self, customer_index, &buffers, 0);
        }

#ifdef BSAL_AGGREGATOR_DEBUG
        BSAL_DEBUG_MARKER("aggregator before flush");
#endif

    }

#ifdef BSAL_AGGREGATOR_DEBUG
    BSAL_DEBUG_MARKER("aggregator after loop");
#endif

    if (concrete_actor->last == 0
                    || concrete_actor->received >= concrete_actor->last + 10000) {

        printf("aggregator/%d received %" PRIu64 " kernel outputs so far\n",
                        bsal_actor_get_name(self),
                        concrete_actor->received);

        concrete_actor->last = concrete_actor->received;
    }

    /* destroy the local copy of the block
     */
    bsal_dna_kmer_block_destroy(&input_block, bsal_actor_get_ephemeral_memory(self));

#ifdef BSAL_AGGREGATOR_DEBUG
        BSAL_DEBUG_MARKER("aggregator marker EXIT");
#endif

    bsal_vector_iterator_init(&iterator, &buffers);

    /* Flush blocks.
     * Destroy blocks and
     * Destroy persistent memory pools, if any.
     */

    i = 0;
    while (bsal_vector_iterator_has_next(&iterator)) {

        bsal_vector_iterator_next(&iterator, (void **)&output_block);

        customer_index = i;

#ifdef BSAL_AGGREGATOR_DEBUG
        printf("aggregator flushing %d\n", customer_index);
#endif

        bsal_aggregator_flush(self, customer_index, &buffers, 1);

        /*
         * Destroy block
         */
        bsal_dna_kmer_block_destroy(output_block, ephemeral_memory);


        i++;
    }

    bsal_vector_iterator_destroy(&iterator);
    bsal_vector_destroy(&buffers);

    bsal_aggregator_verify(self, message);


}

void bsal_aggregator_pack_message(struct bsal_actor *actor, struct bsal_message *message)
{
    void *new_buffer;
    int new_count;
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_message new_message;

    ephemeral_memory = bsal_actor_get_ephemeral_memory(actor);
    new_count = bsal_aggregator_pack_size(actor);
    new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

    bsal_aggregator_pack(actor, new_buffer);

    bsal_message_init(&new_message, BSAL_ACTOR_PACK_REPLY, new_count, new_buffer);
    bsal_actor_send_reply(actor, &new_message);
    bsal_message_destroy(&new_message);

    bsal_memory_pool_free(ephemeral_memory, new_buffer);
    new_buffer = NULL;
}

void bsal_aggregator_unpack_message(struct bsal_actor *actor, struct bsal_message *message)
{
    void *buffer;

    buffer = bsal_message_buffer(message);

    bsal_aggregator_unpack(actor, buffer);

    bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_UNPACK_REPLY);
}

int bsal_aggregator_set_consumers(struct bsal_actor *actor, void *buffer)
{
    struct bsal_vector customers;
    struct bsal_aggregator *concrete_actor;
    int bytes;
    int active_message_multiplier;

    active_message_multiplier = 2;
    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(actor);

    /*
     * receive customer list
     */
    bsal_vector_init(&customers, 0);
    bytes = bsal_vector_unpack(&customers, buffer);

    bsal_actor_helper_add_acquaintances(actor, &customers, &concrete_actor->customers);

    concrete_actor->maximum_active_messages = bsal_vector_size(&concrete_actor->customers) * active_message_multiplier;

    printf("DEBUG45 aggregator %d preparing %d buffers, kmer_length %d\n",
                    bsal_actor_get_name(actor),
                        (int)bsal_vector_size(&concrete_actor->customers),
                        concrete_actor->kmer_length);

#ifdef BSAL_AGGREGATOR_DEBUG
    printf("DEBUG aggregator configured %d customers\n",
                        (int)bsal_vector_size(&concrete_actor->customers));
#endif

    bsal_vector_destroy(&customers);

    return bytes;
}

int bsal_aggregator_pack_unpack(struct bsal_actor *actor, int operation, void *buffer)
{
    struct bsal_packer packer;
    int bytes;
    struct bsal_aggregator *concrete_actor;
    struct bsal_vector consumers;

    concrete_actor = (struct bsal_aggregator *)bsal_actor_concrete_actor(actor);

    bytes = 0;

    bsal_packer_init(&packer, operation, buffer);

    /*
     * Pack the kmer length
     */
    bsal_packer_work(&packer, &concrete_actor->kmer_length, sizeof(concrete_actor->kmer_length));

    bytes += bsal_packer_worked_bytes(&packer);

    /*
    if (operation == BSAL_PACKER_OPERATION_UNPACK) {
        printf("aggregator %d unpacked kmer length %d\n",
                        bsal_actor_get_name(actor),
                        concrete_actor->kmer_length);
    }
*/
    /* Pack the consumers
     */

    if (operation == BSAL_PACKER_OPERATION_UNPACK) {
        bsal_aggregator_set_consumers(actor,
                        ((char *)buffer) + bytes);

    } else {

        bsal_vector_init(&consumers, sizeof(int));
        bsal_actor_helper_get_acquaintances(actor,
                        &concrete_actor->customers,
                        &consumers);

        bytes += bsal_vector_pack_unpack(&consumers,
                        (char *)buffer + bytes,
                        operation);

        bsal_vector_destroy(&consumers);
    }

    bsal_packer_destroy(&packer);

    return bytes;
}

int bsal_aggregator_pack(struct bsal_actor *actor, void *buffer)
{
    return bsal_aggregator_pack_unpack(actor, BSAL_PACKER_OPERATION_PACK, buffer);
}

int bsal_aggregator_unpack(struct bsal_actor *actor, void *buffer)
{
    return bsal_aggregator_pack_unpack(actor, BSAL_PACKER_OPERATION_UNPACK, buffer);
}

int bsal_aggregator_pack_size(struct bsal_actor *actor)
{
    return bsal_aggregator_pack_unpack(actor, BSAL_PACKER_OPERATION_DRY_RUN, NULL);
}


