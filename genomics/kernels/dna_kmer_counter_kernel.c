
#include "dna_kmer_counter_kernel.h"

#include <genomics/storage/sequence_store.h>

#include <genomics/kernels/aggregator.h>
#include <genomics/data/dna_kmer.h>
#include <genomics/data/dna_kmer_block.h>
#include <genomics/data/dna_sequence.h>
#include <genomics/input/input_command.h>

#include <core/helpers/actor_helper.h>
#include <core/helpers/message_helper.h>

#include <core/system/packer.h>
#include <core/system/memory.h>
#include <core/system/timer.h>
#include <core/system/debugger.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

/* options for this kernel
 */
/*
*/

/* debugging options
 */
/*
#define BSAL_KMER_COUNTER_KERNEL_DEBUG
*/

/* Disable memory tracking in memory pool
 * for performance purposes.
 */
#define BSAL_DNA_KMER_COUNTER_KERNEL_DISABLE_TRACKING

#define MAXIMUM_AUTO_SCALING_KERNEL_COUNT 0

struct bsal_script bsal_dna_kmer_counter_kernel_script = {
    .name = BSAL_DNA_KMER_COUNTER_KERNEL_SCRIPT,
    .init = bsal_dna_kmer_counter_kernel_init,
    .destroy = bsal_dna_kmer_counter_kernel_destroy,
    .receive = bsal_dna_kmer_counter_kernel_receive,
    .size = sizeof(struct bsal_dna_kmer_counter_kernel),
    .description = "dna_kmer_counter_kernel"
};

void bsal_dna_kmer_counter_kernel_init(struct bsal_actor *actor)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    concrete_actor->expected = 0;
    concrete_actor->actual = 0;
    concrete_actor->last = 0;
    concrete_actor->blocks = 0;

    concrete_actor->kmer_length = -1;
    concrete_actor->consumer = BSAL_ACTOR_NOBODY;
    concrete_actor->producer = BSAL_ACTOR_NOBODY;
    concrete_actor->producer_source = BSAL_ACTOR_NOBODY;

    concrete_actor->notified = 0;
    concrete_actor->notification_source = 0;

    concrete_actor->kmers = 0;

    bsal_dna_codec_init(&concrete_actor->codec);
    if (bsal_actor_get_node_count(actor) > 1) {
        bsal_dna_codec_enable_two_bit_encoding(&concrete_actor->codec);
    }

    bsal_vector_init(&concrete_actor->kernels, sizeof(int));

    concrete_actor->auto_scaling_in_progress = 0;

    bsal_actor_register(actor, BSAL_ACTOR_PACK,
                    bsal_dna_kmer_counter_kernel_pack_message);
    bsal_actor_register(actor, BSAL_ACTOR_UNPACK,
                    bsal_dna_kmer_counter_kernel_unpack_message);
    bsal_actor_register(actor, BSAL_ACTOR_CLONE_REPLY,
                    bsal_dna_kmer_counter_kernel_clone_reply);
    bsal_actor_register(actor, BSAL_KERNEL_NOTIFY,
                    bsal_dna_kmer_counter_kernel_notify);
    bsal_actor_register(actor, BSAL_KERNEL_NOTIFY_REPLY,
                    bsal_dna_kmer_counter_kernel_notify_reply);
    bsal_actor_register(actor, BSAL_ACTOR_DO_AUTO_SCALING,
                    bsal_dna_kmer_counter_kernel_do_auto_scaling);

    printf("kernel %d is online !!!\n",
                    bsal_actor_get_name(actor));

    /* Enable packing for this actor. Maybe this is already enabled, but who knows.
     */

    bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_PACK_ENABLE);
    concrete_actor->auto_scaling_clone = BSAL_ACTOR_NOBODY;

    concrete_actor->scaling_operations = 0;
}

void bsal_dna_kmer_counter_kernel_destroy(struct bsal_actor *actor)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    concrete_actor->consumer = -1;
    concrete_actor->producer = -1;
    concrete_actor->producer_source =-1;

    bsal_dna_codec_destroy(&concrete_actor->codec);
    bsal_vector_destroy(&concrete_actor->kernels);
}

void bsal_dna_kmer_counter_kernel_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    struct bsal_dna_kmer kmer;
    int name;
    struct bsal_input_command payload;
    void *buffer;
    int entries;
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    int source_index;
    int consumer;
    int i;
    struct bsal_dna_sequence *sequence;
    char *sequence_data;
    struct bsal_vector *command_entries;
    int sequence_length;
    int new_count;
    void *new_buffer;
    struct bsal_message new_message;
    int j;
    int limit;
    int count;
    char saved;
    struct bsal_timer timer;
    struct bsal_dna_kmer_block block;
    int to_reserve;
    int maximum_length;
    int producer;
    struct bsal_memory_pool *ephemeral_memory;

    ephemeral_memory = bsal_actor_get_ephemeral_memory(actor);
    count = bsal_message_count(message);

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);
    tag = bsal_message_tag(message);
    name = bsal_actor_get_name(actor);
    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);

    if (tag == BSAL_PUSH_SEQUENCE_DATA_BLOCK) {

        if (concrete_actor->kmer_length == BSAL_ACTOR_NOBODY) {
            printf("Error no kmer length set in kernel\n");
            return;
        }

        if (concrete_actor->consumer == BSAL_ACTOR_NOBODY) {
            printf("Error no consumer set in kernel\n");
            return;
        }

        if (concrete_actor->producer_source == BSAL_ACTOR_NOBODY) {
            printf("Error, no producer_source set\n");
            return;
        }

        bsal_timer_init(&timer);
        bsal_timer_start(&timer);

        consumer = bsal_actor_get_acquaintance(actor, concrete_actor->consumer);
        source_index = bsal_actor_add_acquaintance(actor, source);

        bsal_input_command_unpack(&payload, buffer, bsal_actor_get_ephemeral_memory(actor),
                        &concrete_actor->codec);

        command_entries = bsal_input_command_entries(&payload);

        entries = bsal_vector_size(command_entries);

        if (entries == 0) {
            printf("Error: kernel received empty payload...\n");
        }

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("DEBUG kernel receives %d entries (%d bytes), kmer length: %d, bytes per object: %d\n",
                        entries, count, concrete_actor->kmer_length,
                        concrete_actor->bytes_per_kmer);
#endif

        to_reserve = 0;

        maximum_length = 0;

        for (i = 0; i < entries; i++) {

            sequence = (struct bsal_dna_sequence *)bsal_vector_at(command_entries, i);

            sequence_length = bsal_dna_sequence_length(sequence);

            if (sequence_length > maximum_length) {
                maximum_length = sequence_length;
            }

            to_reserve += (sequence_length - concrete_actor->kmer_length + 1);
        }

        bsal_dna_kmer_block_init(&block, concrete_actor->kmer_length, source_index, to_reserve);

        sequence_data = bsal_memory_pool_allocate(ephemeral_memory, maximum_length + 1);

        /* extract kmers
         */
        for (i = 0; i < entries; i++) {

            /* TODO improve this */
            sequence = (struct bsal_dna_sequence *)bsal_vector_at(command_entries, i);

            bsal_dna_sequence_get_sequence(sequence, sequence_data,
                            &concrete_actor->codec);

            sequence_length = bsal_dna_sequence_length(sequence);
            limit = sequence_length - concrete_actor->kmer_length + 1;

            for (j = 0; j < limit; j++) {
                saved = sequence_data[j + concrete_actor->kmer_length];
                sequence_data[j + concrete_actor->kmer_length] = '\0';

                bsal_dna_kmer_init(&kmer, sequence_data + j,
                                &concrete_actor->codec, bsal_actor_get_ephemeral_memory(actor));

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG_LEVEL_2
                printf("KERNEL kmer %d,%d %s\n", i, j, sequence_data + j);
#endif

                /*
                 * add kmer in block
                 */
                bsal_dna_kmer_block_add_kmer(&block, &kmer, bsal_actor_get_ephemeral_memory(actor),
                                &concrete_actor->codec);

                bsal_dna_kmer_destroy(&kmer, bsal_actor_get_ephemeral_memory(actor));

                sequence_data[j + concrete_actor->kmer_length] = saved;

                concrete_actor->kmers++;
            }
        }

        bsal_memory_pool_free(ephemeral_memory, sequence_data);
        sequence_data = NULL;

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("after generating kmers\n");
#endif

        concrete_actor->actual += entries;
        concrete_actor->blocks++;

        bsal_input_command_destroy(&payload, bsal_actor_get_ephemeral_memory(actor));

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("consumer%d\n", consumer);
#endif


#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("kernel sends to consumer\n");
        printf("consumer is %d\n", consumer);
#endif

        new_count = bsal_dna_kmer_block_pack_size(&block,
                        &concrete_actor->codec);
        new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);
        bsal_dna_kmer_block_pack(&block, new_buffer,
                        &concrete_actor->codec);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("name %d destination %d PACK with %d bytes\n", name,
                       consumer, new_count);
#endif

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("kernel sends to aggregator");
#endif

        bsal_message_init(&new_message, BSAL_AGGREGATE_KERNEL_OUTPUT,
                        new_count, new_buffer);

        /*
        bsal_message_init(&new_message, BSAL_AGGREGATE_KERNEL_OUTPUT,
                        sizeof(source_index), &source_index);
                        */

        bsal_actor_send(actor, consumer, &new_message);
        bsal_memory_pool_free(ephemeral_memory, new_buffer);

        bsal_actor_helper_send_empty(actor,
                        bsal_actor_get_acquaintance(actor, source_index),
                        BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);

        if (concrete_actor->actual == concrete_actor->expected
                        || concrete_actor->actual >= concrete_actor->last + 300000
                        || concrete_actor->last == 0) {

            printf("kernel %d processed %" PRIu64 " entries (%d blocks) so far\n",
                            name, concrete_actor->actual,
                            concrete_actor->blocks);

            concrete_actor->last = concrete_actor->actual;
        }

        bsal_timer_stop(&timer);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG

        bsal_timer_print(&timer);
#endif

        bsal_timer_destroy(&timer);

        bsal_dna_kmer_block_destroy(&block, bsal_actor_get_ephemeral_memory(actor));

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("leaving call.\n");
#endif

        bsal_dna_kmer_counter_kernel_verify(actor, message);

    } else if (tag == BSAL_AGGREGATE_KERNEL_OUTPUT_REPLY) {

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("kernel receives reply from aggregator\n");
#endif

        /*
        source_index = *(int *)buffer;
        bsal_actor_helper_send_empty(actor,
                        bsal_actor_get_acquaintance(actor, source_index),
                        BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);
                        */

        bsal_dna_kmer_counter_kernel_ask(actor, message);

    } else if (tag == BSAL_ACTOR_START) {

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_START_REPLY);

    } else if (tag == BSAL_RESERVE) {

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kmer counter kernel %d is online !\n", name);
#endif

        concrete_actor->expected = *(uint64_t *)buffer;

        bsal_actor_helper_send_reply_empty(actor, BSAL_RESERVE_REPLY);

    } else if (tag == BSAL_ACTOR_ASK_TO_STOP) {

        printf("kernel/%d generated %" PRIu64 " kmers from %" PRIu64 " entries (%d blocks)\n",
                        bsal_actor_get_name(actor), concrete_actor->kmers,
                        concrete_actor->actual, concrete_actor->blocks);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kernel %d receives request to stop from %d, supervisor is %d\n",
                        name, source, bsal_actor_supervisor(actor));
#endif

        /*bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_STOP);*/

        bsal_actor_helper_ask_to_stop(actor, message);

    } else if (tag == BSAL_ACTOR_SET_CONSUMER) {

        consumer = *(int *)buffer;
        concrete_actor->consumer = bsal_actor_add_acquaintance(actor, consumer);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kernel %d BSAL_ACTOR_SET_CONSUMER consumer %d index %d\n",
                        bsal_actor_get_name(actor), consumer,
                        concrete_actor->consumer);
#endif

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_SET_CONSUMER_REPLY);

    } else if (tag == BSAL_SET_KMER_LENGTH) {

        bsal_message_helper_unpack_int(message, 0, &concrete_actor->kmer_length);

        bsal_dna_kmer_init_mock(&kmer, concrete_actor->kmer_length, &concrete_actor->codec,
                        bsal_actor_get_ephemeral_memory(actor));
        concrete_actor->bytes_per_kmer = bsal_dna_kmer_pack_size(&kmer, concrete_actor->kmer_length,
                        &concrete_actor->codec);
        bsal_dna_kmer_destroy(&kmer, bsal_actor_get_ephemeral_memory(actor));

        bsal_actor_helper_send_reply_empty(actor, BSAL_SET_KMER_LENGTH_REPLY);

    } else if (tag == BSAL_ACTOR_SET_PRODUCER) {

        if (count == 0) {
            printf("Error: kernel needs producer\n");
            return;
        }

        if (concrete_actor->consumer == BSAL_ACTOR_NOBODY) {
            printf("Error: kernel needs a consumer\n");
            return;
        }

        bsal_message_helper_unpack_int(message, 0, &producer);

        concrete_actor->producer = bsal_actor_add_acquaintance(actor, producer);

        bsal_dna_kmer_counter_kernel_ask(actor, message);

        concrete_actor->producer_source = bsal_actor_add_acquaintance(actor, source);

    } else if (tag == BSAL_SEQUENCE_STORE_ASK_REPLY) {

        /* the store has no more sequence...
         */

#ifdef BSAL_DNA_KMER_COUNTER_KERNEL_DEBUG
        printf("DEBUG kernel was told by producer that nothing is left to do\n");
#endif

        bsal_actor_helper_send_empty(actor, bsal_actor_get_acquaintance(actor,
                                concrete_actor->producer_source),
                        BSAL_ACTOR_SET_PRODUCER_REPLY);

    } else if (tag == BSAL_ACTOR_SET_CONSUMER_REPLY) {

        /* Set the producer for the new kernel
         */
        producer = bsal_actor_get_acquaintance(actor, concrete_actor->producer);

        bsal_actor_helper_send_int(actor, concrete_actor->auto_scaling_clone,
                        BSAL_ACTOR_SET_PRODUCER, producer);

        concrete_actor->auto_scaling_in_progress = 0;
        concrete_actor->scaling_operations++;
        concrete_actor->auto_scaling_clone = BSAL_ACTOR_NOBODY;

        printf("kernel %d completed auto-scaling # %d\n",
                        bsal_actor_get_name(actor),
                        concrete_actor->scaling_operations);



    } else if (tag == BSAL_ACTOR_SET_PRODUCER_REPLY
                    && source == concrete_actor->auto_scaling_clone) {

    } else if (tag == BSAL_ACTOR_ENABLE_AUTO_SCALING) {

        printf("AUTO-SCALING kernel %d enables auto-scaling (BSAL_ACTOR_ENABLE_AUTO_SCALING) via actor %d\n",
                        name, source);

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_ENABLE_AUTO_SCALING);

    }
}

void bsal_dna_kmer_counter_kernel_verify(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    if (!concrete_actor->notified) {

        return;
    }

#if 0
    if (concrete_actor->actual != concrete_actor->expected) {
        return;
    }
#endif

    bsal_actor_helper_send_uint64_t(actor, bsal_actor_get_acquaintance(actor,
                            concrete_actor->notification_source),
                    BSAL_KERNEL_NOTIFY_REPLY, concrete_actor->kmers);
}

void bsal_dna_kmer_counter_kernel_ask(struct bsal_actor *self, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    int producer;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(self);

    producer = bsal_actor_get_acquaintance(self, concrete_actor->producer);

    bsal_actor_helper_send_int(self, producer, BSAL_SEQUENCE_STORE_ASK,
                    concrete_actor->kmer_length);

#ifdef BSAL_DNA_KMER_COUNTER_KERNEL_DEBUG
    printf("DEBUG kernel asks producer\n");
#endif
}

void bsal_dna_kmer_counter_kernel_do_auto_scaling(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    int name;
    int source;

    name = bsal_actor_get_name(actor);
    source = bsal_message_source(message);

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    /*
     * Don't do auto-scaling while doing auto-scaling...
     */
    if (concrete_actor->auto_scaling_in_progress) {
        return;
    }

    /*
     * Avoid cloning too many actors
     */
    if (concrete_actor->scaling_operations >= MAXIMUM_AUTO_SCALING_KERNEL_COUNT) {
        return;
    }

    /*
     * Can't scale now because there is no producer
     */
    if (concrete_actor->producer == BSAL_ACTOR_NOBODY) {
        return;
    }

    /* - spawn a kernel
     * - spawn an aggregator
     * - set the aggregator as the consumer of the kernel
     * - set the kmer stores as the consumers of the aggregator
     * - set a sequence store as the producer of the kernel (BSAL_ACTOR_SET_PRODUCER)
     */

    printf("AUTO-SCALING kernel %d receives auto-scale message (BSAL_ACTOR_DO_AUTO_SCALING) via actor %d\n",
                    name, source);

    concrete_actor->auto_scaling_in_progress = 1;

    bsal_actor_helper_send_to_self_int(actor, BSAL_ACTOR_CLONE, name);
}

void bsal_dna_kmer_counter_kernel_pack_message(struct bsal_actor *actor, struct bsal_message *message)
{
    int *new_buffer;
    int new_count;
    struct bsal_message new_message;
    struct bsal_memory_pool *ephemeral_memory;

    ephemeral_memory = bsal_actor_get_ephemeral_memory(actor);
    new_count = bsal_dna_kmer_counter_kernel_pack_size(actor);
    new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

    bsal_dna_kmer_counter_kernel_pack(actor, new_buffer);

    bsal_message_init(&new_message, BSAL_ACTOR_PACK_REPLY, new_count, new_buffer);

    bsal_actor_send_reply(actor, &new_message);

    bsal_message_destroy(&new_message);
    bsal_memory_pool_free(ephemeral_memory, new_buffer);
    new_buffer = NULL;
}

void bsal_dna_kmer_counter_kernel_unpack_message(struct bsal_actor *actor, struct bsal_message *message)
{
    void *buffer;

    buffer = bsal_message_buffer(message);

    bsal_dna_kmer_counter_kernel_unpack(actor, buffer);

    bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_UNPACK_REPLY);
}

void bsal_dna_kmer_counter_kernel_clone_reply(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    int name;
    int clone;
    int source;
    int consumer;
    /*int producer;*/
    int clone_index;

    source = bsal_message_source(message);
    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);
    name = bsal_actor_get_name(actor);
    bsal_message_helper_unpack_int(message, 0, &clone);
    consumer = bsal_actor_get_acquaintance(actor, concrete_actor->consumer);
    /*producer = bsal_actor_get_acquaintance(actor, concrete_actor->producer);*/

    if (source == name) {
        printf("kernel %d cloned itself !!! clone name is %d\n",
                    name, clone);

        bsal_actor_helper_send_int(actor, consumer, BSAL_ACTOR_CLONE, name);

        concrete_actor->auto_scaling_clone = clone;

        clone_index = bsal_actor_add_acquaintance(actor, clone);

        bsal_vector_push_back(&concrete_actor->kernels, &clone_index);

    } else if (source == consumer) {
        printf("kernel %d cloned aggregator %d, clone name is %d\n",
                        name, consumer, clone);

        bsal_actor_helper_send_int(actor, concrete_actor->auto_scaling_clone,
                        BSAL_ACTOR_SET_CONSUMER, clone);

        /*
        printf("kernel %d sending SET_PRODUCER to %d with value %d\n",
                        name, concrete_actor->auto_scaling_clone,
                        producer);
                        */

    }
}


int bsal_dna_kmer_counter_kernel_pack(struct bsal_actor *actor, void *buffer)
{
    return bsal_dna_kmer_counter_kernel_pack_unpack(actor, BSAL_PACKER_OPERATION_PACK, buffer);
}

int bsal_dna_kmer_counter_kernel_unpack(struct bsal_actor *actor, void *buffer)
{
    return bsal_dna_kmer_counter_kernel_pack_unpack(actor, BSAL_PACKER_OPERATION_UNPACK, buffer);
}

int bsal_dna_kmer_counter_kernel_pack_size(struct bsal_actor *actor)
{
    return bsal_dna_kmer_counter_kernel_pack_unpack(actor, BSAL_PACKER_OPERATION_DRY_RUN, NULL);
}

/*
 * Copy these:
 *
 * - kmer length
 * - consumer
 * - producer
 */
int bsal_dna_kmer_counter_kernel_pack_unpack(struct bsal_actor *actor, int operation, void *buffer)
{
    int bytes;
    struct bsal_packer packer;
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    int producer;
    int consumer;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);
    producer = BSAL_ACTOR_NOBODY;
    consumer = BSAL_ACTOR_NOBODY;

    if (operation != BSAL_PACKER_OPERATION_UNPACK) {
        producer = bsal_actor_get_acquaintance(actor, concrete_actor->producer);
        consumer = bsal_actor_get_acquaintance(actor, concrete_actor->consumer);
    }

    bytes = 0;

    bsal_packer_init(&packer, operation, buffer);

    bsal_packer_work(&packer, &concrete_actor->kmer_length, sizeof(concrete_actor->kmer_length));
    bsal_packer_work(&packer, &producer, sizeof(producer));
    bsal_packer_work(&packer, &consumer, sizeof(consumer));

    if (operation == BSAL_PACKER_OPERATION_UNPACK) {

        concrete_actor->producer = bsal_actor_add_acquaintance(actor, producer);
        concrete_actor->consumer = bsal_actor_add_acquaintance(actor, consumer);
    }

    bytes += bsal_packer_worked_bytes(&packer);
    bsal_packer_destroy(&packer);

    return bytes;
}

void bsal_dna_kmer_counter_kernel_notify(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    struct bsal_vector kernels;
    struct bsal_vector_iterator iterator;
    int kernel;
    int source;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    source = bsal_message_source(message);
    concrete_actor->notified = 1;

    bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_DISABLE_AUTO_SCALING);

    concrete_actor->notification_source = bsal_actor_add_acquaintance(actor, source);

    if (concrete_actor->scaling_operations > 0) {

        concrete_actor->sum_of_kmers = 0;
        concrete_actor->sum_of_kmers += concrete_actor->kmers;
        concrete_actor->notified_children = 0;

        bsal_vector_init(&kernels, sizeof(int));

        bsal_actor_helper_get_acquaintances(actor, &concrete_actor->kernels,
                        &kernels);

        bsal_vector_iterator_init(&iterator, &kernels);

        while (bsal_vector_iterator_get_next_value(&iterator, &kernel)) {

            bsal_actor_helper_send_empty(actor, kernel, BSAL_KERNEL_NOTIFY);
        }

        bsal_vector_iterator_destroy(&iterator);

        bsal_vector_destroy(&kernels);

    } else {
        bsal_dna_kmer_counter_kernel_verify(actor, message);
    }
}

void bsal_dna_kmer_counter_kernel_notify_reply(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;
    uint64_t kmers;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    bsal_message_helper_unpack_uint64_t(message, 0, &kmers);

    concrete_actor->sum_of_kmers += kmers;

    concrete_actor->notified_children++;

    if (concrete_actor->notified_children == bsal_vector_size(&concrete_actor->kernels)) {


        bsal_actor_helper_send_uint64_t(actor, bsal_actor_get_acquaintance(actor,
                            concrete_actor->notification_source),
                    BSAL_KERNEL_NOTIFY_REPLY, concrete_actor->sum_of_kmers);

    }

}
