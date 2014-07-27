
#include "sequence_store.h"

#include <genomics/input/input_command.h>
#include <genomics/data/dna_sequence.h>

#include <core/structures/vector_iterator.h>
#include <core/helpers/actor_helper.h>
#include <core/helpers/message_helper.h>
#include <core/system/memory.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <inttypes.h>

/*
#define BSAL_SEQUENCE_STORE_DEBUG
*/

#define MINIMUM_PERIOD 4096

struct bsal_script bsal_sequence_store_script = {
    .name = BSAL_SEQUENCE_STORE_SCRIPT,
    .init = bsal_sequence_store_init,
    .destroy = bsal_sequence_store_destroy,
    .receive = bsal_sequence_store_receive,
    .size = sizeof(struct bsal_sequence_store),
    .description = "sequence_store"
};

void bsal_sequence_store_init(struct bsal_actor *actor)
{
    struct bsal_sequence_store *concrete_actor;

    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);
    bsal_vector_init(&concrete_actor->sequences, sizeof(struct bsal_dna_sequence));

    concrete_actor->required_kmers = -1;

    printf("DEBUG sequence store %d is online on node %d\n",
                    bsal_actor_get_name(actor),
                    bsal_actor_node_name(actor));
#ifdef BSAL_SEQUENCE_STORE_DEBUG
#endif

    concrete_actor->received = 0;

    bsal_dna_codec_init(&concrete_actor->codec);
    if (bsal_actor_get_node_count(actor) > 1) {
        bsal_dna_codec_enable_two_bit_encoding(&concrete_actor->codec);
    }

    bsal_actor_register(actor, BSAL_SEQUENCE_STORE_ASK,
                    bsal_sequence_store_ask);

    concrete_actor->iterator_started = 0;
    concrete_actor->reservation_producer = -1;

    /* 2^26 */
    bsal_memory_pool_init(&concrete_actor->persistent_memory, 67108864);
    bsal_memory_pool_disable_tracking(&concrete_actor->persistent_memory);

    concrete_actor->left = -1;
    concrete_actor->last = -1;

    concrete_actor->progress_supervisor = BSAL_ACTOR_NOBODY;
}

void bsal_sequence_store_destroy(struct bsal_actor *actor)
{
    struct bsal_sequence_store *concrete_actor;
    struct bsal_vector_iterator iterator;
    struct bsal_dna_sequence *sequence;

    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

    bsal_vector_iterator_init(&iterator, &concrete_actor->sequences);

    while (bsal_vector_iterator_has_next(&iterator)) {

        bsal_vector_iterator_next(&iterator, (void**)&sequence);
        bsal_dna_sequence_destroy(sequence, &concrete_actor->persistent_memory);
    }

    bsal_vector_destroy(&concrete_actor->sequences);
    bsal_dna_codec_destroy(&concrete_actor->codec);

    if (concrete_actor->iterator_started) {
        bsal_vector_iterator_destroy(&concrete_actor->iterator);
        concrete_actor->iterator_started = 0;
    }

    bsal_memory_pool_destroy(&concrete_actor->persistent_memory);
}

void bsal_sequence_store_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    struct bsal_sequence_store *concrete_actor;

    tag = bsal_message_tag(message);
    source = bsal_message_source(message);
    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

    if (tag == BSAL_PUSH_SEQUENCE_DATA_BLOCK) {

        bsal_sequence_store_push_sequence_data_block(actor, message);

    } else if (tag == BSAL_RESERVE) {

        bsal_sequence_store_reserve(actor, message);

    } else if (tag == BSAL_ACTOR_ASK_TO_STOP) {

#ifdef BSAL_SEQUENCE_STORE_DEBUG
        printf("DEBUG store %d dies\n",
                        bsal_actor_get_name(actor));
#endif

        bsal_actor_helper_ask_to_stop(actor, message);

    } else if (tag == BSAL_SEQUENCE_STORE_REQUEST_PROGRESS) {

        concrete_actor->progress_supervisor = bsal_actor_add_acquaintance(actor, source);
    }
}

void bsal_sequence_store_push_sequence_data_block(struct bsal_actor *actor, struct bsal_message *message)
{
    uint64_t first;
    /*
    uint64_t last;
    */
    struct bsal_vector *new_entries;
    struct bsal_input_command payload;
    struct bsal_sequence_store *concrete_actor;
    void *buffer;
    int64_t i;

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    int count;
#endif

    struct bsal_dna_sequence *bucket_in_message;
    struct bsal_dna_sequence *bucket_in_store;

    buffer = bsal_message_buffer(message);
    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    count = bsal_message_count(message);
    printf("DEBUG store receives BSAL_PUSH_SEQUENCE_DATA_BLOCK %d bytes\n",
                    count);
#endif

    bsal_input_command_unpack(&payload, buffer, bsal_actor_get_ephemeral_memory(actor),
                    &concrete_actor->codec);

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    printf("DEBUG store %d bsal_sequence_store_receive command:\n",
                    bsal_actor_get_name(actor));

    bsal_input_command_print(&payload);
#endif

    first = bsal_input_command_store_first(&payload);
    /*
    last = bsal_input_command_store_last(&payload);
    */
    new_entries = bsal_input_command_entries(&payload);

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    printf("DEBUG store %d bsal_sequence_store_push_sequence_data_block entries %d\n",
                    bsal_actor_get_name(actor),
                    (int)bsal_vector_size(new_entries));
#endif

    for (i = 0; i < bsal_vector_size(new_entries); i++) {

        if (concrete_actor->received % 1000000 == 0) {
            bsal_sequence_store_show_progress(actor, message);
        }

        bucket_in_message = (struct bsal_dna_sequence *)bsal_vector_at(new_entries,
                        i);

        bucket_in_store = (struct bsal_dna_sequence *)bsal_vector_at(&concrete_actor->sequences,
                        first + i);

        /* join the bucket, this load DNA into the store
         */
        /*
        *bucket_in_store = *bucket_in_message;
        */

#ifdef BSAL_SEQUENCE_STORE_DEBUG
        if (i == 0) {
            printf("DEBUG first in payload\n");
            printf("DEBUG-thor i %d bucket_in_store %p bucket_in_message %p\n",
                        (int)i,
                        (void *)bucket_in_store, (void *)bucket_in_message);

            printf("DEBUG i %d first %d size %d store size %d\n",
                   (int)i, (int)first,
                   (int)bsal_vector_size(new_entries),
                   (int)bsal_vector_size(&concrete_actor->sequences));

            bsal_dna_sequence_print(bucket_in_message);
        }
#endif

        bsal_dna_sequence_init_copy(bucket_in_store, bucket_in_message,
                        &concrete_actor->codec, &concrete_actor->persistent_memory);

        concrete_actor->received++;

#ifdef BSAL_SEQUENCE_STORE_DEBUG
        printf("%" PRId64 "/%" PRId64 "\n",
                        concrete_actor->received,
                        bsal_vector_size(&concrete_actor->sequences));
#endif

        if (concrete_actor->received == concrete_actor->expected) {
            bsal_sequence_store_show_progress(actor, message);

            concrete_actor->left = concrete_actor->received;
            concrete_actor->last = 0;
        }
    }

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    printf("DONE.\n");
#endif

    /* The DNA sequences are kept and are not
     * destroyed.
     */
    /* free payload
     */
    bsal_input_command_destroy(&payload, bsal_actor_get_ephemeral_memory(actor));

    bsal_actor_helper_send_reply_empty(actor, BSAL_PUSH_SEQUENCE_DATA_BLOCK_REPLY);
}

void bsal_sequence_store_reserve(struct bsal_actor *actor, struct bsal_message *message)
{
    uint64_t amount;
    int i;
    void *buffer;
    struct bsal_dna_sequence *dna_sequence;
    struct bsal_sequence_store *concrete_actor;
    int source;

    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);
    amount = *(uint64_t*)buffer;
    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

    concrete_actor->expected = amount;

    concrete_actor->reservation_producer = bsal_actor_add_acquaintance(actor, source);
    printf("DEBUG store %d reserves %" PRIu64 " buckets\n",
                    bsal_actor_get_name(actor),
                    amount);

    bsal_vector_resize(&concrete_actor->sequences, amount);

#ifdef BSAL_SEQUENCE_STORE_DEBUG
    printf("DEBUG store %d now has %d buckets\n",
                    bsal_actor_get_name(actor),
                    (int)bsal_vector_size(&concrete_actor->sequences));
#endif

    for ( i = 0; i < bsal_vector_size(&concrete_actor->sequences); i++) {
        /*
         * initialize sequences with empty things
         */
        dna_sequence = (struct bsal_dna_sequence *)bsal_vector_at(&concrete_actor->sequences,
                        i);

        bsal_dna_sequence_init(dna_sequence, NULL, &concrete_actor->codec, &concrete_actor->persistent_memory);
    }

    bsal_actor_helper_send_reply_empty(actor, BSAL_RESERVE_REPLY);

    if (concrete_actor->expected == 0) {

        bsal_sequence_store_show_progress(actor, message);
    }
}

void bsal_sequence_store_show_progress(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_sequence_store *concrete_actor;

    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

    printf("sequence store %d has %" PRId64 "/%" PRId64 " entries\n",
                    bsal_actor_get_name(actor),
                    concrete_actor->received,
                    bsal_vector_size(&concrete_actor->sequences));

    if (concrete_actor->received == concrete_actor->expected) {

        bsal_actor_helper_send_empty(actor, bsal_actor_get_acquaintance(actor,
                                concrete_actor->reservation_producer),
                        BSAL_SEQUENCE_STORE_READY);
    }
}

void bsal_sequence_store_ask(struct bsal_actor *self, struct bsal_message *message)
{
    struct bsal_sequence_store *concrete_actor;
    struct bsal_input_command payload;
    struct bsal_dna_sequence *sequence;
    int new_count;
    void *new_buffer;
    struct bsal_message new_message;
    int entry_count;
    float completion;
    int name;
    int period;
    struct bsal_memory_pool *ephemeral_memory;
    double ratio;
    int kmer_length;
    int required_kmers;
    int kmers;
    int length;
    int sequence_kmers;

    required_kmers = bsal_sequence_store_get_required_kmers(self, message);
    bsal_message_helper_unpack_int(message, 0, &kmer_length);

    name = bsal_actor_get_name(self);
#ifdef BSAL_SEQUENCE_STORE_DEBUG
#endif

    ephemeral_memory = bsal_actor_get_ephemeral_memory(self);
    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(self);

    if (concrete_actor->received != concrete_actor->expected) {
        printf("Error: sequence store %d is not ready %" PRIu64 "/%" PRIu64 " (reservation producer %d)\n",
                        name,
                        concrete_actor->received, concrete_actor->expected,
                        concrete_actor->reservation_producer);
    }



    if (!concrete_actor->iterator_started) {
        bsal_vector_iterator_init(&concrete_actor->iterator, &concrete_actor->sequences);

        concrete_actor->iterator_started = 1;
    }

    /* the metadata are not important here.
     * This is just a container.
     */
    bsal_input_command_init(&payload, -1, 0, 0);

    kmers = 0;

    while ( bsal_vector_iterator_has_next(&concrete_actor->iterator)
                    && kmers < required_kmers) {

        bsal_vector_iterator_next(&concrete_actor->iterator, (void **)&sequence);

        /*printf("ADDING %d\n", i);*/
        bsal_input_command_add_entry(&payload, sequence, &concrete_actor->codec,
                        bsal_actor_get_ephemeral_memory(self));

        length = bsal_dna_sequence_length(sequence);

        sequence_kmers = length - kmer_length + 1;
        kmers += sequence_kmers;

        /*
        printf("Yielded %d kmers... %d/%d\n",
                        sequence_kmers, kmers, required_kmers);
                        */
    }

    entry_count = bsal_input_command_entry_count(&payload);

    if (entry_count > 0) {
        new_count = bsal_input_command_pack_size(&payload,
                        &concrete_actor->codec);
        new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

        bsal_input_command_pack(&payload, new_buffer,
                        &concrete_actor->codec);

        bsal_message_init(&new_message, BSAL_PUSH_SEQUENCE_DATA_BLOCK, new_count, new_buffer);

        bsal_actor_send_reply(self, &new_message);
        bsal_message_destroy(&new_message);

#ifdef BSAL_SEQUENCE_STORE_DEBUG
        printf("store/%d fulfill order\n", name);
#endif

        bsal_memory_pool_free(ephemeral_memory, new_buffer);

        concrete_actor->left -= entry_count;

    } else {
        bsal_actor_helper_send_reply_empty(self, BSAL_SEQUENCE_STORE_ASK_REPLY);
#ifdef BSAL_SEQUENCE_STORE_DEBUG
        printf("store/%d can not fulfill order\n", name);
#endif
    }

    /* Show something every 0.05
     */
    period = concrete_actor->received / 20;

    if (period < MINIMUM_PERIOD) {
        period = MINIMUM_PERIOD;
    }

    if (concrete_actor->last >= 0
                    && (concrete_actor->last == 0
                    || concrete_actor->left < concrete_actor->last - period
                    || concrete_actor->left == 0)) {

        completion = (concrete_actor->left + 0.0) / concrete_actor->received;

        printf("sequence store %d has %" PRId64 "/%" PRId64 " (%.2f) entries left to produce\n",
                        name,
                        concrete_actor->left, concrete_actor->received,
                        completion);

        concrete_actor->last = concrete_actor->left;

        if (concrete_actor->last == 0) {
            concrete_actor->last = -1;
        }
    }

    bsal_input_command_destroy(&payload, bsal_actor_get_ephemeral_memory(self));

    /*
     * Send a progress report to the supervisor of progression
     */
    if (concrete_actor->progress_supervisor != BSAL_ACTOR_NOBODY) {
        ratio = (concrete_actor->received - concrete_actor->left + 0.0) / concrete_actor->received;

        if (ratio >= 0.16) {

            bsal_actor_helper_send_double(self, bsal_actor_get_acquaintance(self,
                                    concrete_actor->progress_supervisor),
                            BSAL_SEQUENCE_STORE_REQUEST_PROGRESS_REPLY,
                            ratio);
            concrete_actor->progress_supervisor = BSAL_ACTOR_NOBODY;
        }
    }
}

int bsal_sequence_store_get_required_kmers(struct bsal_actor *actor, struct bsal_message *message)
{

    size_t maximum_number_of_bytes;
    size_t maximum_number_of_bytes_per_worker;
    size_t sum_of_buffer_sizes;
    int minimum_end_buffer_size_in_bytes;
    int minimum_end_buffer_size_in_nucleotides;
    int minimum_end_buffer_size_in_ascii_kmers;
    int total_workers;
    int total_kmer_stores;
    int nodes;
    int workers;
    struct bsal_sequence_store *concrete_actor;
    int kmer_length;

    concrete_actor = (struct bsal_sequence_store *)bsal_actor_concrete_actor(actor);

    if (concrete_actor->required_kmers != -1) {
        return concrete_actor->required_kmers;
    }

    workers = bsal_actor_node_worker_count(actor);
    nodes = bsal_actor_get_node_count(actor);
    total_workers = nodes * workers;
    total_kmer_stores = total_workers * 1;
    bsal_message_helper_unpack_int(message, 0, &kmer_length);

    /* 4 KiB */
    minimum_end_buffer_size_in_bytes = BSAL_SEQUENCE_STORE_FINAL_BLOCK_SIZE;

    /* Maximum number of bytes for a node
     */
    /*maximum_number_of_bytes = 2147483648;*/
    /*maximum_number_of_bytes = 1073741824;*/
    maximum_number_of_bytes = 536870912;
    maximum_number_of_bytes_per_worker = maximum_number_of_bytes / workers;

    sum_of_buffer_sizes = minimum_end_buffer_size_in_bytes * total_kmer_stores;

    if (sum_of_buffer_sizes > maximum_number_of_bytes_per_worker) {
        minimum_end_buffer_size_in_bytes = maximum_number_of_bytes_per_worker / total_kmer_stores;
    }

    sum_of_buffer_sizes = minimum_end_buffer_size_in_bytes * total_kmer_stores;

    printf("INFO Workers: %d Consumers: %d BufferSizeForConsumer: %d BufferSizeForWorker: %zu\n",
                    workers,
                    total_kmer_stores,
                    minimum_end_buffer_size_in_bytes,
                    sum_of_buffer_sizes);

    /* Assume 1 byte per nucleotide since transportation does not use 2-bit encoding in the
     * DNA codec.
     *
     * However, the 2-bit DNA codec is used for the graph.
     *
     * Update: use 2-bit encoding everywhere.
     */

    minimum_end_buffer_size_in_nucleotides = minimum_end_buffer_size_in_bytes;

    if (bsal_actor_get_node_count(actor) > 1) {
        minimum_end_buffer_size_in_nucleotides *= 4;
    }

    minimum_end_buffer_size_in_ascii_kmers = minimum_end_buffer_size_in_nucleotides / kmer_length;

    concrete_actor->required_kmers = minimum_end_buffer_size_in_ascii_kmers * total_kmer_stores;

    return concrete_actor->required_kmers;
}
