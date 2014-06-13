
#include "kmer_counter_kernel.h"

#include <data/dna_kmer.h>
#include <storage/sequence_store.h>
#include <input/input_command.h>
#include <helpers/actor_helper.h>
#include <patterns/aggregator.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

/*
#define BSAL_KMER_COUNTER_KERNEL_ENABLED
*/

struct bsal_script bsal_kmer_counter_kernel_script = {
    .name = BSAL_KMER_COUNTER_KERNEL_SCRIPT,
    .init = bsal_kmer_counter_kernel_init,
    .destroy = bsal_kmer_counter_kernel_destroy,
    .receive = bsal_kmer_counter_kernel_receive,
    .size = sizeof(struct bsal_kmer_counter_kernel)
};

void bsal_kmer_counter_kernel_init(struct bsal_actor *actor)
{
    struct bsal_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    concrete_actor->expected = 0;
    concrete_actor->actual = 0;
    concrete_actor->last = 0;
    concrete_actor->customer = 0;
}

void bsal_kmer_counter_kernel_destroy(struct bsal_actor *actor)
{
    struct bsal_kmer_counter_kernel *concrete_actor;

    concrete_actor = (struct bsal_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);

    concrete_actor->customer = 0;
}

void bsal_kmer_counter_kernel_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    struct bsal_dna_kmer kmer;
    int name;
    struct bsal_input_command payload;
    void *buffer;
    int entries;
    struct bsal_kmer_counter_kernel *concrete_actor;
    int source_index;

    concrete_actor = (struct bsal_kmer_counter_kernel *)bsal_actor_concrete_actor(actor);
    tag = bsal_message_tag(message);
    name = bsal_actor_name(actor);
    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);

    if (tag == BSAL_PUSH_SEQUENCE_DATA_BLOCK) {

        bsal_input_command_unpack(&payload, buffer);

        entries = bsal_vector_size(bsal_input_command_entries(&payload));

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("DEBUG kernel receives %d entries\n", entries);
#endif

        source_index = bsal_actor_add_acquaintance(actor, source);

        concrete_actor->actual += entries;

        if (concrete_actor->actual == concrete_actor->expected
                        || concrete_actor->actual > concrete_actor->last + 1000000
                        || concrete_actor->last == 0) {

            printf("kernel actor/%d processed %" PRIu64 "/%" PRIu64 " entries so far\n",
                            name, concrete_actor->actual, concrete_actor->expected);

            concrete_actor->last = concrete_actor->actual;
        }

        bsal_dna_kmer_init(&kmer, NULL);
        bsal_input_command_destroy(&payload);

#if BSAL_KMER_COUNTER_KERNEL_ENABLED
        bsal_actor_helper_send_int(actor, bsal_actor_get_acquaintance(actor,
                                concrete_actor->customer),
                        BSAL_AGGREGATE_KERNEL_OUTPUT,
                        source_index);
#else

        bsal_actor_helper_send_empty(actor,
                        bsal_actor_get_acquaintance(actor, source_index),
                        BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);
#endif

    } else if (tag == BSAL_AGGREGATE_KERNEL_OUTPUT_REPLY) {

        source_index = *(int *)buffer;
        bsal_actor_helper_send_empty(actor,
                        bsal_actor_get_acquaintance(actor, source_index),
                        BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);

    } else if (tag == BSAL_ACTOR_START) {

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_START_REPLY);

    } else if (tag == BSAL_SEQUENCE_STORE_RESERVE) {

        printf("kmer counter kernel actor/%d is online !\n", name);

        concrete_actor->expected = *(uint64_t *)buffer;

        bsal_actor_helper_send_reply_empty(actor, BSAL_SEQUENCE_STORE_RESERVE_REPLY);

    } else if (tag == BSAL_ACTOR_ASK_TO_STOP
                    && source == bsal_actor_supervisor(actor)) {

#ifdef BSAL_KMER_COUNTER_KERNEL_DEBUG
        printf("kernel actor/%d receives request to stop from actor/%d, supervisor is actor/%d\n",
                        name, source, bsal_actor_supervisor(actor));
#endif

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_STOP);

    } else if (tag == BSAL_SET_CUSTOMER) {

        concrete_actor->customer = *(int *)buffer;

        bsal_actor_helper_send_reply_empty(actor, BSAL_SET_CUSTOMER_REPLY);
    }
}


