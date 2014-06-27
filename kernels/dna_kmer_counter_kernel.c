
#include "dna_kmer_counter_kernel.h"

#include <storage/sequence_store.h>

#include <data/dna_kmer.h>
#include <data/dna_kmer_block.h>
#include <data/dna_sequence.h>
#include <input/input_command.h>

#include <helpers/actor_helper.h>
#include <helpers/message_helper.h>

#include <patterns/aggregator.h>
#include <system/memory.h>
#include <system/timer.h>
#include <system/debugger.h>

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
    concrete_actor->consumer = -1;
    concrete_actor->producer = -1;
    concrete_actor->producer_source =-1;

    concrete_actor->notified = 0;
    concrete_actor->notification_source = 0;

    concrete_actor->kmers = 0;

    bsal_memory_pool_init(&concrete_actor->ephemeral_memory, 2097152);

#ifdef BSAL_DNA_KMER_COUNTER_KERNEL_DISABLE_TRACKING
    bsal_memory_pool_disable_tracking(&concrete_actor->ephemeral_memory);
#endif

    bsal_dna_codec_init(&concrete_actor->codec);
}

void bsal_dna_kmer_counter_kernel_destroy(struct bsal_actor *actor)
{
    struct bsal_dna_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    concrete_actor->consumer = -1;
    concrete_actor->producer = -1;
    concrete_actor->producer_source =-1;

    bsal_dna_codec_destroy(&concrete_actor->codec);

    bsal_memory_pool_destroy(&concrete_actor->ephemeral_memory);
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

    count = bsal_message_count(message);

    concrete_actor = (struct bsal_dna_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);
    tag = bsal_message_tag(message);
    name = bsal_actor_name(actor);
    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);

#ifdef BSAL_DNA_KMER_COUNTER_KERNEL_DISABLE_TRACKING
    /* Release all memory allocations before doing anything.
     * Tracking is disabled anyway.
     */
    bsal_memory_pool_free_all(&concrete_actor->ephemeral_memory);
#endif

    if (tag == BSAL_PUSH_SEQUENCE_DATA_BLOCK) {

        if (concrete_actor->kmer_length == -1) {
            printf("Error no kmer length set in kernel\n");
            return;
        }

        if (concrete_actor->consumer == -1) {
            printf("Error no consumer set in kernel\n");
            return;
        }

        bsal_timer_init(&timer);
        bsal_timer_start(&timer);

        consumer = bsal_actor_get_acquaintance(actor, concrete_actor->consumer);
        source_index = bsal_actor_add_acquaintance(actor, source);

        bsal_input_command_unpack(&payload, buffer, &concrete_actor->ephemeral_memory);

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

        sequence_data = bsal_allocate(maximum_length + 1);

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
                                &concrete_actor->codec, &concrete_actor->ephemeral_memory);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG_LEVEL_2
                printf("KERNEL kmer %d,%d %s\n", i, j, sequence_data + j);
#endif

                /*
                 * add kmer in block
                 */
                bsal_dna_kmer_block_add_kmer(&block, &kmer, &concrete_actor->ephemeral_memory);

                bsal_dna_kmer_destroy(&kmer, &concrete_actor->ephemeral_memory);

                sequence_data[j + concrete_actor->kmer_length] = saved;

                concrete_actor->kmers++;
            }
        }

        bsal_free(sequence_data);
        sequence_data = NULL;

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("after generating kmers\n");
#endif

        concrete_actor->actual += entries;
        concrete_actor->blocks++;

        bsal_input_command_destroy(&payload, &concrete_actor->ephemeral_memory);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("consumer%d\n", consumer);
#endif


#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("kernel sends to consumer\n");
        printf("consumer is %d\n", consumer);
#endif

        new_count = bsal_dna_kmer_block_pack_size(&block);
        new_buffer = bsal_allocate(new_count);
        bsal_dna_kmer_block_pack(&block, new_buffer);

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
        bsal_free(new_buffer);

        bsal_actor_helper_send_empty(actor,
                        bsal_actor_get_acquaintance(actor, source_index),
                        BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);

        if (concrete_actor->actual == concrete_actor->expected
                        || concrete_actor->actual >= concrete_actor->last + 10000
                        || concrete_actor->last == 0) {

            printf("kernel %d processed %" PRIu64 "/%" PRIu64 " entries (%d blocks) so far\n",
                            name, concrete_actor->actual,
                            concrete_actor->expected,
                            concrete_actor->blocks);

            concrete_actor->last = concrete_actor->actual;
        }

        bsal_timer_stop(&timer);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG

        bsal_timer_print(&timer);
#endif

        bsal_timer_destroy(&timer);

        bsal_dna_kmer_block_destroy(&block, &concrete_actor->ephemeral_memory);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        BSAL_DEBUG_MARKER("leaving call.\n");
#endif

        bsal_dna_kmer_counter_kernel_verify(actor, message);

        bsal_dna_kmer_counter_kernel_ask(actor, message);

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
                        bsal_actor_name(actor), concrete_actor->kmers,
                        concrete_actor->expected, concrete_actor->blocks);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kernel %d receives request to stop from %d, supervisor is %d\n",
                        name, source, bsal_actor_supervisor(actor));
#endif

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_STOP);

    } else if (tag == BSAL_ACTOR_SET_CONSUMER) {

        consumer = *(int *)buffer;
        concrete_actor->consumer = bsal_actor_add_acquaintance(actor, consumer);

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kernel %d BSAL_ACTOR_SET_CONSUMER consumer %d index %d\n",
                        bsal_actor_name(actor), consumer,
                        concrete_actor->consumer);
#endif

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_SET_CONSUMER_REPLY);

    } else if (tag == BSAL_SET_KMER_LENGTH) {

        bsal_message_helper_unpack_int(message, 0, &concrete_actor->kmer_length);

        bsal_dna_kmer_init_mock(&kmer, concrete_actor->kmer_length, &concrete_actor->codec,
                        &concrete_actor->ephemeral_memory);
        concrete_actor->bytes_per_kmer = bsal_dna_kmer_pack_size(&kmer, concrete_actor->kmer_length);
        bsal_dna_kmer_destroy(&kmer, &concrete_actor->ephemeral_memory);

        bsal_actor_helper_send_reply_empty(actor, BSAL_SET_KMER_LENGTH_REPLY);

    } else if (tag == BSAL_KERNEL_NOTIFY) {

        concrete_actor->notified = 1;

        concrete_actor->notification_source = bsal_actor_add_acquaintance(actor, source);

        bsal_dna_kmer_counter_kernel_verify(actor, message);

    } else if (tag == BSAL_ACTOR_SET_PRODUCER) {

        if (count == 0) {
            printf("Error: kernel needs producer\n");
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

    bsal_actor_helper_send_empty(self, producer, BSAL_SEQUENCE_STORE_ASK);

#ifdef BSAL_DNA_KMER_COUNTER_KERNEL_DEBUG
    printf("DEBUG kernel asks producer\n");
#endif
}
