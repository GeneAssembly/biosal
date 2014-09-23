
#include "assembly_graph_store.h"

#include "assembly_arc_kernel.h"
#include "assembly_arc_block.h"

#include "assembly_vertex.h"

/*
 * Include storage actors for message
 * tags.
 */
#include <genomics/storage/sequence_store.h>
#include <genomics/storage/kmer_store.h>

#include <genomics/kernels/dna_kmer_counter_kernel.h>

#include <genomics/data/dna_kmer.h>
#include <genomics/data/dna_kmer_block.h>
#include <genomics/data/dna_kmer_frequency_block.h>

#include <core/helpers/message_helper.h>
#include <core/system/memory.h>

#include <core/structures/vector.h>
#include <core/structures/vector_iterator.h>

#include <core/system/debugger.h>

#include <engine/thorium/scheduler/scheduling_queue.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BSAL_DEBUG_ISSUE_540

struct thorium_script bsal_assembly_graph_store_script = {
    .identifier = SCRIPT_ASSEMBLY_GRAPH_STORE,
    .name = "bsal_assembly_graph_store",
    .init = bsal_assembly_graph_store_init,
    .destroy = bsal_assembly_graph_store_destroy,
    .receive = bsal_assembly_graph_store_receive,
    .size = sizeof(struct bsal_assembly_graph_store),
    .author = "Sebastien Boisvert",
    .description = "Build a distributed assembly graph with actors and active messages",
    .version = "Stable"
};

void bsal_assembly_graph_store_init(struct thorium_actor *self)
{
    struct bsal_assembly_graph_store *concrete_self;

#if 0
    thorium_actor_set_priority(self, THORIUM_PRIORITY_MAX);
#endif

    concrete_self = thorium_actor_concrete_actor(self);

    concrete_self->consumed_canonical_vertex_count = 0;

    concrete_self->kmer_length = -1;
    concrete_self->received = 0;

    bsal_assembly_graph_summary_init(&concrete_self->graph_summary);

    bsal_dna_codec_init(&concrete_self->transport_codec);

    /*
     * When running with only 1 node, the transport codec and storage codec
     * are different.
     */

    concrete_self->codec_are_different = 1;

    if (bsal_dna_codec_must_use_two_bit_encoding(&concrete_self->transport_codec,
                            thorium_actor_get_node_count(self))) {
        concrete_self->codec_are_different = 0;
        bsal_dna_codec_enable_two_bit_encoding(&concrete_self->transport_codec);
    }

    bsal_dna_codec_init(&concrete_self->storage_codec);

/* This option enables 2-bit encoding
 * for kmers.
 */
    bsal_dna_codec_enable_two_bit_encoding(&concrete_self->storage_codec);

    thorium_actor_add_action(self, ACTION_YIELD_REPLY, bsal_assembly_graph_store_yield_reply);
    thorium_actor_add_action(self, ACTION_PUSH_KMER_BLOCK,
                    bsal_assembly_graph_store_push_kmer_block);

    thorium_actor_add_action(self, ACTION_ASSEMBLY_PUSH_ARC_BLOCK,
                    bsal_assembly_graph_store_push_arc_block);

    thorium_actor_add_action(self, ACTION_ASSEMBLY_GET_SUMMARY,
                    bsal_assembly_graph_store_get_summary);

    thorium_actor_add_action(self, ACTION_ASSEMBLY_GET_VERTEX,
                    bsal_assembly_graph_store_get_vertex);

    thorium_actor_add_action(self, ACTION_ASSEMBLY_GET_STARTING_KMER,
                    bsal_assembly_graph_store_get_starting_vertex);

    thorium_actor_add_action(self, ACTION_MARK_VERTEX_AS_VISITED,
                    bsal_assembly_graph_store_mark_vertex_as_visited);
    thorium_actor_add_action(self, ACTION_SET_VERTEX_FLAG,
                    bsal_assembly_graph_store_set_vertex_flag);

    concrete_self->printed_vertex_size = 0;
    concrete_self->printed_arc_size = 0;

    concrete_self->last_received = 0;
    concrete_self->received_arc_block_count = 0;
    concrete_self->received_arc_count = 0;
    concrete_self->summary_in_progress = 0;

    concrete_self->unitig_vertex_count = 0;
}

void bsal_assembly_graph_store_destroy(struct thorium_actor *self)
{
    struct bsal_assembly_graph_store *concrete_self;

    concrete_self = thorium_actor_concrete_actor(self);

    bsal_assembly_graph_summary_destroy(&concrete_self->graph_summary);

    if (concrete_self->kmer_length != -1) {
        bsal_map_destroy(&concrete_self->table);
    }

    bsal_dna_codec_destroy(&concrete_self->transport_codec);
    bsal_dna_codec_destroy(&concrete_self->storage_codec);

    concrete_self->kmer_length = -1;
}

void bsal_assembly_graph_store_receive(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;
    /*void *buffer;*/
    struct bsal_assembly_graph_store *concrete_self;
    double value;
    struct bsal_dna_kmer kmer;
    /*struct bsal_memory_pool *ephemeral_memory;*/
    int customer;
    int big_key_size;
    int big_value_size;

    if (thorium_actor_take_action(self, message)) {
        return;
    }

    /*ephemeral_memory = thorium_actor_get_ephemeral_memory(self);*/
    concrete_self = thorium_actor_concrete_actor(self);

    tag = thorium_message_action(message);
    /*buffer = thorium_message_buffer(message);*/

    if (tag == ACTION_SET_KMER_LENGTH) {

        thorium_message_unpack_int(message, 0, &concrete_self->kmer_length);

        bsal_dna_kmer_init_mock(&kmer, concrete_self->kmer_length,
                        &concrete_self->storage_codec, thorium_actor_get_ephemeral_memory(self));
        concrete_self->key_length_in_bytes = bsal_dna_kmer_pack_size(&kmer,
                        concrete_self->kmer_length, &concrete_self->storage_codec);
        bsal_dna_kmer_destroy(&kmer, thorium_actor_get_ephemeral_memory(self));

        big_key_size = concrete_self->key_length_in_bytes;
        big_value_size = sizeof(struct bsal_assembly_vertex);
        bsal_map_init(&concrete_self->table, big_key_size,
                        big_value_size);

        printf("DEBUG big_key_size %d big_value_size %d\n", big_key_size, big_value_size);

        /*
         * Configure the map for better performance.
         */
        bsal_map_disable_deletion_support(&concrete_self->table);

        /*
         * The threshold of the map is not very important because
         * requests that hit the map have to first arrive as messages,
         * which are slow.
         */
        bsal_map_set_threshold(&concrete_self->table, 0.95);

        thorium_actor_send_reply_empty(self, ACTION_SET_KMER_LENGTH_REPLY);

    } else if (tag == ACTION_ASSEMBLY_GET_KMER_LENGTH) {

        thorium_actor_send_reply_int(self, ACTION_ASSEMBLY_GET_KMER_LENGTH_REPLY,
                        concrete_self->kmer_length);

    } else if (tag == ACTION_RESET) {

        /*
         * Reset the iterator.
         */
        bsal_map_iterator_init(&concrete_self->iterator, &concrete_self->table);

        printf("DEBUG unitig_vertex_count %d\n",
                        concrete_self->unitig_vertex_count);

        thorium_actor_send_reply_empty(self, ACTION_RESET_REPLY);


    } else if (tag == ACTION_SEQUENCE_STORE_REQUEST_PROGRESS_REPLY) {

        thorium_message_unpack_double(message, 0, &value);

        bsal_map_set_current_size_estimate(&concrete_self->table, value);

    } else if (tag == ACTION_ASK_TO_STOP) {

        printf("%s/%d received %d arc blocks\n",
                        thorium_actor_script_name(self),
                        thorium_actor_name(self),
                        concrete_self->received_arc_block_count);

        thorium_actor_ask_to_stop(self, message);

    } else if (tag == ACTION_SET_CONSUMER) {

        thorium_message_unpack_int(message, 0, &customer);

        printf("%s/%d will use coverage distribution %d\n",
                        thorium_actor_script_name(self),
                        thorium_actor_name(self), customer);

        concrete_self->customer = customer;

        thorium_actor_send_reply_empty(self, ACTION_SET_CONSUMER_REPLY);

    } else if (tag == ACTION_PUSH_DATA) {

        printf("%s/%d receives ACTION_PUSH_DATA\n",
                        thorium_actor_script_name(self),
                        thorium_actor_name(self));

        bsal_assembly_graph_store_push_data(self, message);

    } else if (tag == ACTION_STORE_GET_ENTRY_COUNT) {

        thorium_actor_send_reply_uint64_t(self, ACTION_STORE_GET_ENTRY_COUNT_REPLY,
                        concrete_self->received);

    } else if (tag == ACTION_GET_RECEIVED_ARC_COUNT) {

        thorium_actor_send_reply_uint64_t(self, ACTION_GET_RECEIVED_ARC_COUNT_REPLY,
                        concrete_self->received_arc_count);
    }
}

void bsal_assembly_graph_store_print(struct thorium_actor *self)
{
    struct bsal_map_iterator iterator;
    struct bsal_dna_kmer kmer;
    void *key;
    struct bsal_assembly_vertex *value;
    int coverage;
    char *sequence;
    struct bsal_assembly_graph_store *concrete_self;
    int maximum_length;
    int length;
    struct bsal_memory_pool *ephemeral_memory;

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);

    bsal_map_iterator_init(&iterator, &concrete_self->table);

    printf("map size %d\n", (int)bsal_map_size(&concrete_self->table));

    maximum_length = 0;

    while (bsal_map_iterator_has_next(&iterator)) {
        bsal_map_iterator_next(&iterator, (void **)&key, (void **)&value);

        bsal_dna_kmer_init_empty(&kmer);
        bsal_dna_kmer_unpack(&kmer, key, concrete_self->kmer_length,
                        thorium_actor_get_ephemeral_memory(self),
                        &concrete_self->storage_codec);

        length = bsal_dna_kmer_length(&kmer, concrete_self->kmer_length);

        /*
        printf("length %d\n", length);
        */
        if (length > maximum_length) {
            maximum_length = length;
        }
        bsal_dna_kmer_destroy(&kmer, thorium_actor_get_ephemeral_memory(self));
    }

    /*
    printf("MAx length %d\n", maximum_length);
    */

    sequence = bsal_memory_pool_allocate(ephemeral_memory, maximum_length + 1);
    sequence[0] = '\0';
    bsal_map_iterator_destroy(&iterator);
    bsal_map_iterator_init(&iterator, &concrete_self->table);

    while (bsal_map_iterator_has_next(&iterator)) {
        bsal_map_iterator_next(&iterator, (void **)&key, (void **)&value);

        bsal_dna_kmer_init_empty(&kmer);
        bsal_dna_kmer_unpack(&kmer, key, concrete_self->kmer_length,
                        thorium_actor_get_ephemeral_memory(self),
                        &concrete_self->storage_codec);

        bsal_dna_kmer_get_sequence(&kmer, sequence, concrete_self->kmer_length,
                        &concrete_self->storage_codec);

        coverage = bsal_assembly_vertex_coverage_depth(value);

        printf("Sequence %s Coverage %d\n", sequence, coverage);

        bsal_dna_kmer_destroy(&kmer, thorium_actor_get_ephemeral_memory(self));
    }

    bsal_map_iterator_destroy(&iterator);
    bsal_memory_pool_free(ephemeral_memory, sequence);
}

void bsal_assembly_graph_store_push_data(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    int name;
    int source;

    concrete_self = thorium_actor_concrete_actor(self);
    source = thorium_message_source(message);
    concrete_self->source = source;
    name = thorium_actor_name(self);

    bsal_map_init(&concrete_self->coverage_distribution, sizeof(int), sizeof(uint64_t));

    printf("%s/%d: local table has %" PRIu64" canonical kmers (%" PRIu64 " kmers)\n",
                        thorium_actor_script_name(self),
                    name, bsal_map_size(&concrete_self->table),
                    2 * bsal_map_size(&concrete_self->table));

    bsal_map_iterator_init(&concrete_self->iterator, &concrete_self->table);

    thorium_actor_send_to_self_empty(self, ACTION_YIELD);
}

void bsal_assembly_graph_store_yield_reply(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_dna_kmer kmer;
    void *key;
    struct bsal_assembly_vertex *value;
    int coverage;
    int customer;
    uint64_t *count;
    int new_count;
    void *new_buffer;
    struct thorium_message new_message;
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_assembly_graph_store *concrete_self;
    int i;
    int max;

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);
    customer = concrete_self->customer;

#if 0
    printf("YIELD REPLY\n");
#endif

    i = 0;
    max = 1024;

    key = NULL;
    value = NULL;

    while (i < max
                    && bsal_map_iterator_has_next(&concrete_self->iterator)) {

        bsal_map_iterator_next(&concrete_self->iterator, (void **)&key, (void **)&value);

        bsal_dna_kmer_init_empty(&kmer);
        bsal_dna_kmer_unpack(&kmer, key, concrete_self->kmer_length,
                        ephemeral_memory,
                        &concrete_self->storage_codec);

        coverage = bsal_assembly_vertex_coverage_depth(value);

        count = (uint64_t *)bsal_map_get(&concrete_self->coverage_distribution, &coverage);

        if (count == NULL) {

            count = (uint64_t *)bsal_map_add(&concrete_self->coverage_distribution, &coverage);

            (*count) = 0;
        }

        /* increment for the lowest kmer (canonical) */
        (*count)++;

        bsal_dna_kmer_destroy(&kmer, ephemeral_memory);

        ++i;
    }

    /* yield again if the iterator is not at the end
     */
    if (bsal_map_iterator_has_next(&concrete_self->iterator)) {

#if 0
        printf("yield ! %d\n", i);
#endif

        thorium_actor_send_to_self_empty(self, ACTION_YIELD);

        return;
    }

    /*
    printf("ready...\n");
    */

    bsal_map_iterator_destroy(&concrete_self->iterator);

    new_count = bsal_map_pack_size(&concrete_self->coverage_distribution);

    new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

    bsal_map_pack(&concrete_self->coverage_distribution, new_buffer);

    printf("SENDING %s/%d sends map to %d, %d bytes / %d entries\n",
                        thorium_actor_script_name(self),
                    thorium_actor_name(self),
                    customer, new_count,
                    (int)bsal_map_size(&concrete_self->coverage_distribution));

    thorium_message_init(&new_message, ACTION_PUSH_DATA, new_count, new_buffer);

    thorium_actor_send(self, customer, &new_message);
    thorium_message_destroy(&new_message);

    bsal_map_destroy(&concrete_self->coverage_distribution);

    thorium_actor_send_empty(self, concrete_self->source,
                            ACTION_PUSH_DATA_REPLY);

    bsal_memory_pool_free(ephemeral_memory, new_buffer);
}

void bsal_assembly_graph_store_push_kmer_block(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_dna_kmer_frequency_block block;
    struct bsal_assembly_vertex *bucket;
    void *packed_kmer;
    struct bsal_map_iterator iterator;
    struct bsal_assembly_graph_store *concrete_self;
    /*int tag;*/
    void *key;
    struct bsal_map *kmers;
    struct bsal_dna_kmer kmer;
    void *buffer;
    int count;
    struct bsal_dna_kmer encoded_kmer;
    char *raw_kmer;
    int period;
    struct bsal_dna_kmer *kmer_pointer;
    int *frequency;

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);
    /*tag = thorium_message_action(message);*/
    buffer = thorium_message_buffer(message);
    count = thorium_message_count(message);

    /*
     * Handler for PUSH_DATA
     */

    bsal_dna_kmer_frequency_block_init(&block, concrete_self->kmer_length,
                    ephemeral_memory, &concrete_self->transport_codec, 0);

    bsal_dna_kmer_frequency_block_unpack(&block, buffer, thorium_actor_get_ephemeral_memory(self),
                    &concrete_self->transport_codec);

    key = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->key_length_in_bytes);

    kmers = bsal_dna_kmer_frequency_block_kmers(&block);
    bsal_map_iterator_init(&iterator, kmers);

    period = 2500000;

    raw_kmer = bsal_memory_pool_allocate(thorium_actor_get_ephemeral_memory(self),
                    concrete_self->kmer_length + 1);

    if (!concrete_self->printed_vertex_size) {

        printf("DEBUG VERTEX DELIVERY %d bytes\n", count);

        concrete_self->printed_vertex_size = 1;
    }

    while (bsal_map_iterator_has_next(&iterator)) {

        /*
         * add kmers to store
         */
        bsal_map_iterator_next(&iterator, (void **)&packed_kmer, (void **)&frequency);

        /* Store the kmer in 2 bit encoding
         */

        bsal_dna_kmer_init_empty(&kmer);
        bsal_dna_kmer_unpack(&kmer, packed_kmer, concrete_self->kmer_length,
                    ephemeral_memory,
                    &concrete_self->transport_codec);

        kmer_pointer = &kmer;

        if (concrete_self->codec_are_different) {
            /*
             * Get a copy of the sequence
             */
            bsal_dna_kmer_get_sequence(kmer_pointer, raw_kmer, concrete_self->kmer_length,
                        &concrete_self->transport_codec);


            bsal_dna_kmer_init(&encoded_kmer, raw_kmer, &concrete_self->storage_codec,
                        thorium_actor_get_ephemeral_memory(self));
            kmer_pointer = &encoded_kmer;
        }

        bsal_dna_kmer_pack_store_key(kmer_pointer, key,
                        concrete_self->kmer_length, &concrete_self->storage_codec,
                        thorium_actor_get_ephemeral_memory(self));

#ifdef BSAL_DEBUG_ISSUE_540
        if (strcmp(raw_kmer, "AGCTGGTAGTCATCACCAGACTGGAACAG") == 0
                        || strcmp(raw_kmer, "CGCGATCTGTTGCTGGGCCTAACGTGGTA") == 0
                        || strcmp(raw_kmer, "TACCACGTTAGGCCCAGCAACAGATCGCG") == 0) {
            printf("Examine store key for %s\n", raw_kmer);

            bsal_debugger_examine(key, concrete_self->key_length_in_bytes);
        }
#endif

        bucket = bsal_map_get(&concrete_self->table, key);

        if (bucket == NULL) {
            /* This is the first time that this kmer is seen.
             */
            bucket = bsal_map_add(&concrete_self->table, key);

            bsal_assembly_vertex_init(bucket);

#if 0
            printf("DEBUG303 ADD_KEY");
            bsal_dna_kmer_print(&encoded_kmer, concrete_self->kmer_length,
                            &concrete_self->storage_codec, ephemeral_memory);
#endif
        }

        if (concrete_self->codec_are_different) {
            bsal_dna_kmer_destroy(&encoded_kmer,
                        thorium_actor_get_ephemeral_memory(self));
        }

        bsal_dna_kmer_destroy(&kmer, ephemeral_memory);

        bsal_assembly_vertex_increase_coverage_depth(bucket, *frequency);

        if (concrete_self->received >= concrete_self->last_received + period) {
            printf("%s/%d received %" PRIu64 " kmers so far,"
                            " store has %" PRIu64 " canonical kmers, %" PRIu64 " kmers\n",
                        thorium_actor_script_name(self),
                            thorium_actor_name(self), concrete_self->received,
                            bsal_map_size(&concrete_self->table),
                            2 * bsal_map_size(&concrete_self->table));

            concrete_self->last_received = concrete_self->received;
        }

        concrete_self->received += *frequency;
    }

    bsal_memory_pool_free(ephemeral_memory, key);
    bsal_memory_pool_free(ephemeral_memory, raw_kmer);

    bsal_map_iterator_destroy(&iterator);
    bsal_dna_kmer_frequency_block_destroy(&block, thorium_actor_get_ephemeral_memory(self));

    thorium_actor_send_reply_empty(self, ACTION_PUSH_KMER_BLOCK_REPLY);
}

void bsal_assembly_graph_store_push_arc_block(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    int size;
    int i;
    void *buffer;
    int count;
    struct bsal_assembly_arc_block input_block;
    struct bsal_assembly_arc *arc;
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_vector *input_arcs;
    char *sequence;
    void *key;

#if 0
    /*
     * Don't do anything to rule out that this is the problem.
     */
    thorium_actor_send_reply_empty(self, ACTION_ASSEMBLY_PUSH_ARC_BLOCK_REPLY);
    return;
#endif

    concrete_self = thorium_actor_concrete_actor(self);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);

    sequence = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->kmer_length + 1);

    ++concrete_self->received_arc_block_count;

    count = thorium_message_count(message);
    buffer = thorium_message_buffer(message);

    bsal_assembly_arc_block_init(&input_block, ephemeral_memory, concrete_self->kmer_length,
                    &concrete_self->transport_codec);

    bsal_assembly_arc_block_unpack(&input_block, buffer, concrete_self->kmer_length,
                    &concrete_self->transport_codec, ephemeral_memory);

    input_arcs = bsal_assembly_arc_block_get_arcs(&input_block);
    size = bsal_vector_size(input_arcs);

    if (!concrete_self->printed_arc_size) {
        printf("DEBUG ARC DELIVERY %d bytes, %d arcs\n",
                    count, size);

        concrete_self->printed_arc_size = 1;
    }

    key = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->key_length_in_bytes);

    for (i = 0; i < size; i++) {

        arc = bsal_vector_at(input_arcs, i);

#ifdef BSAL_ASSEMBLY_ADD_ARCS
        bsal_assembly_graph_store_add_arc(self, arc, sequence, key);
#endif

        ++concrete_self->received_arc_count;
    }

    bsal_memory_pool_free(ephemeral_memory, key);

    bsal_assembly_arc_block_destroy(&input_block, ephemeral_memory);

    /*
     *
     * Add the arcs to the graph
     */

    thorium_actor_send_reply_empty(self, ACTION_ASSEMBLY_PUSH_ARC_BLOCK_REPLY);

    bsal_memory_pool_free(ephemeral_memory, sequence);
}

void bsal_assembly_graph_store_add_arc(struct thorium_actor *self,
                struct bsal_assembly_arc *arc, char *sequence,
                void *key)
{
    struct bsal_assembly_graph_store *concrete_self;
    struct bsal_dna_kmer *source;
    struct bsal_dna_kmer real_source;
    int destination;
    int type;
    struct bsal_assembly_vertex *vertex;
    struct bsal_memory_pool *ephemeral_memory;
    int is_canonical;

#if 0
    /*
     * Don't do anything just to see if this code
     * is buggy.
     */

    return;
#endif

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_ARC
    int verbose;
#endif

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_ARC
    verbose = 0;

    if (concrete_self->received_arc_count == 0) {
        verbose = 1;
    }

    if (verbose) {
        printf("DEBUG BioSAL::GraphStore::AddArc\n");

        bsal_assembly_arc_print(arc, concrete_self->kmer_length, &concrete_self->transport_codec,
                    ephemeral_memory);
    }
#endif

    source = bsal_assembly_arc_source(arc);
    destination = bsal_assembly_arc_destination(arc);
    type = bsal_assembly_arc_type(arc);

    /*
     * Don't convert the data if the transport codec and the
     * storage codec are the same.
     */
    if (concrete_self->codec_are_different) {
        bsal_dna_kmer_get_sequence(source, sequence, concrete_self->kmer_length,
                        &concrete_self->transport_codec);
        bsal_dna_kmer_init(&real_source, sequence, &concrete_self->storage_codec,
                        ephemeral_memory);

        source = &real_source;
    }

    bsal_dna_kmer_pack_store_key(source, key,
                        concrete_self->kmer_length, &concrete_self->storage_codec,
                        ephemeral_memory);

    vertex = bsal_map_get(&concrete_self->table, key);

#ifdef BSAL_DEBUGGER_ENABLE_ASSERT
    if (vertex == NULL) {
        printf("Error: vertex is NULL, key_length_in_bytes %d size %" PRIu64 "\n",
                        concrete_self->key_length_in_bytes,
                        bsal_map_size(&concrete_self->table));
    }
#endif

    BSAL_DEBUGGER_ASSERT(vertex != NULL);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_ARC
    if (verbose) {
        printf("DEBUG BEFORE:\n");
        bsal_assembly_vertex_print(vertex);
    }
#endif

    /*
     * Inverse the arc if the source is not canonical
     */
    is_canonical = bsal_dna_kmer_is_canonical(source, concrete_self->kmer_length,
                    &concrete_self->storage_codec);

    if (!is_canonical) {

        if (type == BSAL_ARC_TYPE_PARENT) {
            type = BSAL_ARC_TYPE_CHILD;

        } else if (type == BSAL_ARC_TYPE_CHILD) {

            type = BSAL_ARC_TYPE_PARENT;
        }

        destination = bsal_dna_codec_get_complement(destination);
    }

    if (type == BSAL_ARC_TYPE_PARENT) {

        bsal_assembly_vertex_add_parent(vertex, destination);

    } else if (type == BSAL_ARC_TYPE_CHILD) {

        bsal_assembly_vertex_add_child(vertex, destination);
    }

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_ARC
    if (verbose) {
        printf("DEBUG AFTER:\n");
        bsal_assembly_vertex_print(vertex);
    }
#endif

    if (concrete_self->codec_are_different) {
        bsal_dna_kmer_destroy(&real_source, ephemeral_memory);
    }
}

void bsal_assembly_graph_store_get_summary(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    int source;

    source = thorium_message_source(message);
    concrete_self = thorium_actor_concrete_actor(self);

    concrete_self->summary_in_progress = 1;
    concrete_self->source_for_summary = source;

    thorium_actor_add_action_with_condition(self, ACTION_YIELD_REPLY,
                    bsal_assembly_graph_store_yield_reply_summary,
                    &concrete_self->summary_in_progress, 1);

    bsal_map_iterator_init(&concrete_self->iterator, &concrete_self->table);

    thorium_actor_send_to_self_empty(self, ACTION_YIELD);
}

void bsal_assembly_graph_store_yield_reply_summary(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    int limit;
    int processed;
    int new_count;
    void *new_buffer;
    struct bsal_assembly_vertex *vertex;
    int coverage;
    int parent_count;
    int child_count;
    struct bsal_memory_pool *ephemeral_memory;
    /*int child_count;*/

    concrete_self = thorium_actor_concrete_actor(self);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);

    limit = 4321;
    processed = 0;

    /*
     * This loop gather canonical information only.
     */
    while (processed < limit
                    && bsal_map_iterator_has_next(&concrete_self->iterator)) {

        bsal_map_iterator_next(&concrete_self->iterator, NULL, (void **)&vertex);

        coverage = bsal_assembly_vertex_coverage_depth(vertex);

        parent_count = bsal_assembly_vertex_parent_count(vertex);
        child_count = bsal_assembly_vertex_child_count(vertex);

        /*
         * Don't count any real arc twice.
         */
        /*
        child_count = bsal_assembly_vertex_child_count(vertex);
        concrete_self->arc_count += child_count;
        */

        /*
         * Gather degree information too !
         */

        bsal_assembly_graph_summary_add(&concrete_self->graph_summary, coverage, parent_count, child_count);
        bsal_assembly_graph_summary_add(&concrete_self->graph_summary, coverage, child_count, parent_count);

        ++processed;
    }

    if (bsal_map_iterator_has_next(&concrete_self->iterator)) {

        thorium_actor_send_to_self_empty(self, ACTION_YIELD);

    } else {

        /*
         * Send the answer
         */

        new_count = bsal_assembly_graph_summary_pack_size(&concrete_self->graph_summary);
        new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);
        bsal_assembly_graph_summary_pack(&concrete_self->graph_summary, new_buffer);
        thorium_actor_send_buffer(self, concrete_self->source_for_summary,
                        ACTION_ASSEMBLY_GET_SUMMARY_REPLY, new_count, new_buffer);
        bsal_memory_pool_free(ephemeral_memory, new_buffer);

        /*
         * Reset the iterator.
         */

        bsal_map_iterator_destroy(&concrete_self->iterator);
        bsal_map_iterator_init(&concrete_self->iterator, &concrete_self->table);
    }
}

void bsal_assembly_graph_store_get_vertex(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_vertex vertex;
    struct bsal_dna_kmer kmer;
    void *buffer;
    struct bsal_assembly_graph_store *concrete_self;
    struct bsal_memory_pool *ephemeral_memory;
    struct thorium_message new_message;
    int new_count;
    void *new_buffer;
    struct bsal_assembly_vertex *canonical_vertex;
    int is_canonical;
    int source;
    int path;
    int position;
    int count;

    path = -1;
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);

    source = thorium_message_source(message);
    buffer = thorium_message_buffer(message);
    count = thorium_message_count(message);

    bsal_dna_kmer_init_empty(&kmer);

    position = 0;
    position += bsal_dna_kmer_unpack(&kmer, buffer, concrete_self->kmer_length,
                ephemeral_memory,
                &concrete_self->transport_codec);

    /*
     * Check if a path index was provided too.
     */
    if (position < count) {
        position += thorium_message_unpack_int(message, position, &path);
    }

    BSAL_DEBUGGER_ASSERT_IS_EQUAL_INT(position, count);
    BSAL_DEBUGGER_ASSERT(position == count);

    canonical_vertex = bsal_assembly_graph_store_find_vertex(self, &kmer);

    bsal_assembly_vertex_init_copy(&vertex, canonical_vertex);

    is_canonical = bsal_dna_kmer_is_canonical(&kmer, concrete_self->kmer_length,
                    &concrete_self->transport_codec);

    if (!is_canonical) {

        bsal_assembly_vertex_invert_arcs(&vertex);
    }

    bsal_dna_kmer_destroy(&kmer, ephemeral_memory);

    new_count = bsal_assembly_vertex_pack_size(&vertex);
    new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

    bsal_assembly_vertex_pack(&vertex, new_buffer);

    thorium_message_init(&new_message, ACTION_ASSEMBLY_GET_VERTEX_REPLY,
                    new_count, new_buffer);

    thorium_actor_send_reply(self, &new_message);

    thorium_message_destroy(&new_message);
    bsal_memory_pool_free(ephemeral_memory, new_buffer);
}

void bsal_assembly_graph_store_get_starting_vertex(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    struct bsal_dna_kmer transport_kmer;
    struct bsal_dna_kmer storage_kmer;
    struct bsal_memory_pool *ephemeral_memory;
    struct thorium_message new_message;
    int new_count;
    void *new_buffer;
    char *sequence;
    void *storage_key;
    struct bsal_assembly_vertex *vertex;
    int source;

    concrete_self = thorium_actor_concrete_actor(self);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);

    source = thorium_message_source(message);

    while (bsal_map_iterator_has_next(&concrete_self->iterator)) {

        storage_key = NULL;
        vertex = NULL;

        bsal_map_iterator_next(&concrete_self->iterator, (void **)&storage_key,
                        (void **)&vertex);

        /*
         * Skip the vertex if it does have the status
         * BSAL_VERTEX_FLAG_USED.
         */
        if (bsal_assembly_vertex_get_flag(vertex, BSAL_VERTEX_FLAG_USED)) {


            continue;
        }

        BSAL_DEBUGGER_ASSERT(storage_key != NULL);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_GET_STARTING_VERTEX
        printf("From storage\n");
        bsal_assembly_vertex_print(vertex);

        bsal_debugger_examine(storage_key, concrete_self->key_length_in_bytes);
#endif

        bsal_dna_kmer_init_empty(&storage_kmer);
        bsal_dna_kmer_unpack(&storage_kmer, storage_key, concrete_self->kmer_length,
                        ephemeral_memory,
                        &concrete_self->storage_codec);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_GET_STARTING_VERTEX
        printf("DEBUG starting kmer Storage kmer hash %" PRIu64 "\n",
                        bsal_dna_kmer_hash(&storage_kmer, concrete_self->kmer_length,
                                &concrete_self->storage_codec));

        bsal_dna_kmer_print(&storage_kmer, concrete_self->kmer_length, &concrete_self->storage_codec,
                    ephemeral_memory);
#endif

        sequence = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->kmer_length + 1);

        bsal_dna_kmer_get_sequence(&storage_kmer, sequence, concrete_self->kmer_length,
                        &concrete_self->storage_codec);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_GET_STARTING_VERTEX
        printf("SEQUENCE %s\n", sequence);
#endif

        bsal_dna_kmer_init(&transport_kmer, sequence, &concrete_self->transport_codec,
                        ephemeral_memory);

        new_count = bsal_dna_kmer_pack_size(&transport_kmer, concrete_self->kmer_length,
                        &concrete_self->transport_codec);
        new_buffer = bsal_memory_pool_allocate(ephemeral_memory, new_count);

        bsal_dna_kmer_pack(&transport_kmer, new_buffer, concrete_self->kmer_length,
                        &concrete_self->transport_codec);

#ifdef BSAL_ASSEMBLY_GRAPH_STORE_DEBUG_GET_STARTING_VERTEX
        printf("Packed version:\n");

        bsal_debugger_examine(new_buffer, new_count);

        printf("TRANSPORT Kmer new_count %d\n", new_count);

        bsal_dna_kmer_print(&transport_kmer, concrete_self->kmer_length, &concrete_self->transport_codec,
                    ephemeral_memory);
#endif

        thorium_message_init(&new_message, ACTION_ASSEMBLY_GET_STARTING_KMER_REPLY,
                        new_count, new_buffer);

        thorium_actor_send_reply(self, &new_message);

        thorium_message_destroy(&new_message);

        bsal_dna_kmer_destroy(&transport_kmer, ephemeral_memory);

        bsal_memory_pool_free(ephemeral_memory, sequence);

        bsal_dna_kmer_destroy(&storage_kmer, ephemeral_memory);
        bsal_memory_pool_free(ephemeral_memory, new_buffer);

        return;
    }

    /*
     * An empty reply means that the store has nothing more to yield.
     */
    thorium_actor_send_reply_empty(self, ACTION_ASSEMBLY_GET_STARTING_KMER_REPLY);
}

/*
 * Limit the number of graph stores to avoid running out of memory with all these buffers.
 * At 1024 nodes and 15 graph store per node (and 15 typical kernels per node too),
 * the memory usage per node for communication alone is
 *
 * irb(main):001:0> 15*1024*4096*15
 * => 943718400
 */

int bsal_assembly_graph_store_get_store_count_per_node(struct thorium_actor *self)
{
#ifdef __bgq__
    int powerpc_a2_processor_core_count;
    int nodes;

    nodes = thorium_actor_get_node_count(self);

    /*
     * The A2 chip has 18 cores (0-17).
     *
     * Cores 0-15 are for applications.
     * Core 16 is used by the operating system only.
     * Core 17 is for manufacturing yield.
     */
    powerpc_a2_processor_core_count = 16;

    /*
     * The A2 chip has 16 cores, one of them is used
     * by the Thorium core.
     *
     * 512 nodes -> 512 * 8 graph stores
     * 1024 nodes -> 1024 * 8 graph stores
     * 2048 nodes -> 2048 * 6 graph stores
     */


    if (1 * 512 <= nodes && nodes <= 3 * 512) {

        return powerpc_a2_processor_core_count / 2;
    }

    return powerpc_a2_processor_core_count / 2 - 2;
#else

    /*
     * Right now, there is no limit for other systems.
     * The policy is to use one graph store per worker.
     */
    return thorium_actor_node_worker_count(self);
#endif
}

void bsal_assembly_graph_store_print_progress(struct thorium_actor *self)
{
    struct bsal_assembly_graph_store *concrete_self;
    uint64_t total;
    uint64_t stride;
    uint64_t current_value;
    int steps;
    float ratio;
    char finished[] = " FINISHED";
    char not_finished[] = "";
    char *state;

    concrete_self = thorium_actor_concrete_actor(self);

    total = bsal_map_size(&concrete_self->table);
    steps = 20;
    stride = total / steps;

    current_value = concrete_self->consumed_canonical_vertex_count;

    if ((current_value == 0)
                    || (current_value % stride == 0)
                    || (current_value == total)) {

        state = not_finished;

        if (current_value == total) {
            state = finished;
        }

        ratio = (0.0 + current_value) / total;

        printf("%s/%d %.2f of vertices were consumed%s\n",
                        thorium_actor_script_name(self),
                        thorium_actor_name(self), ratio, state);
    }
}

void bsal_assembly_graph_store_mark_as_used(struct thorium_actor *self,
                struct bsal_assembly_vertex *vertex, int source, int path)
{
    struct bsal_assembly_graph_store *concrete_self;

    BSAL_DEBUGGER_ASSERT(source >= 0);
    BSAL_DEBUGGER_ASSERT(path >= 0);

    concrete_self = thorium_actor_concrete_actor(self);

    if (!bsal_assembly_vertex_get_flag(vertex, BSAL_VERTEX_FLAG_USED)) {
        bsal_assembly_vertex_set_flag(vertex, BSAL_VERTEX_FLAG_USED);
        ++concrete_self->consumed_canonical_vertex_count;
        bsal_assembly_graph_store_print_progress(self);
    }

#if 0
    printf("%s set last_actor %d last_path_index %d\n",
                    thorium_actor_script_name(self),
                    source, path);
#endif

    bsal_assembly_vertex_set_last_actor(vertex, source, path);
}

void bsal_assembly_graph_store_mark_vertex_as_visited(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_graph_store *concrete_self;
    char *buffer;
    int source;
    int path_index;
    char *sequence;
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_dna_kmer kmer;
    struct bsal_dna_kmer storage_kmer;
    struct bsal_assembly_vertex *canonical_vertex;
    int position;
    void *key;
    int force;

    force = 1;
    position = 0;
    concrete_self = thorium_actor_concrete_actor(self);
    source = thorium_message_source(message);
    buffer = thorium_message_buffer(message);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);

    /*
     * Get the kmer.
     */
    bsal_dna_kmer_init_empty(&kmer);

    position += bsal_dna_kmer_unpack(&kmer, buffer, concrete_self->kmer_length,
                ephemeral_memory, &concrete_self->transport_codec);
    sequence = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->kmer_length + 1);
    bsal_dna_kmer_get_sequence(&kmer, sequence, concrete_self->kmer_length,
                        &concrete_self->transport_codec);
    bsal_dna_kmer_init(&storage_kmer, sequence, &concrete_self->storage_codec,
                        ephemeral_memory);

    /*
     * Get store key
     */
    key = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->key_length_in_bytes);
    bsal_dna_kmer_pack_store_key(&storage_kmer, key, concrete_self->kmer_length, &concrete_self->storage_codec,
                        ephemeral_memory);

    /* Get vertex. */
    canonical_vertex = bsal_map_get(&concrete_self->table, key);

    bsal_dna_kmer_destroy(&kmer, ephemeral_memory);
    bsal_dna_kmer_destroy(&storage_kmer, ephemeral_memory);
    bsal_memory_pool_free(ephemeral_memory, key);
    bsal_memory_pool_free(ephemeral_memory, sequence);

    position += thorium_message_unpack_int(message, position, &path_index);
    /*
     * At this point, mark the vertex with flag BSAL_VERTEX_FLAG_USED
     * so that any other actor that attempt to grab it will have to communicate
     * with the actor.
     */

    /*
     * This is a good idea to always update with the last one.
     */
    if (force || !bsal_assembly_vertex_get_flag(canonical_vertex, BSAL_VERTEX_FLAG_USED)) {
        bsal_assembly_graph_store_mark_as_used(self, canonical_vertex, source, path_index);
    }
#if 0
#endif

    thorium_actor_send_reply_empty(self, ACTION_MARK_VERTEX_AS_VISITED_REPLY);
}

void bsal_assembly_graph_store_set_vertex_flag(struct thorium_actor *self,
                struct thorium_message *message)
{
    char *buffer;
    int count;
    int flag;
    struct bsal_dna_kmer transport_kmer;
    struct bsal_assembly_vertex *vertex;
    struct bsal_assembly_graph_store *concrete_self;
    struct bsal_memory_pool *ephemeral_memory;
    int position;

    concrete_self = thorium_actor_concrete_actor(self);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    bsal_dna_kmer_init_empty(&transport_kmer);
    buffer = thorium_message_buffer(message);
    count = thorium_message_count(message);

    position = 0;
    position += bsal_dna_kmer_unpack(&transport_kmer, buffer, concrete_self->kmer_length,
                    ephemeral_memory, &concrete_self->storage_codec);
    bsal_memory_copy(&flag, buffer + position, sizeof(flag));
    position += sizeof(flag);
    BSAL_DEBUGGER_ASSERT(position == count);

    BSAL_DEBUGGER_ASSERT(flag >= BSAL_VERTEX_FLAG_START_VALUE);
    BSAL_DEBUGGER_ASSERT(flag <= BSAL_VERTEX_FLAG_END_VALUE);

    if (flag == BSAL_VERTEX_FLAG_UNITIG) {
        ++concrete_self->unitig_vertex_count;
    }

#if 0
    printf("DEBUG ACTION_SET_VERTEX_FLAG %d\n", flag);
#endif
    vertex = bsal_assembly_graph_store_find_vertex(self, &transport_kmer);

    bsal_dna_kmer_destroy(&transport_kmer, ephemeral_memory);

    bsal_assembly_vertex_set_flag(vertex, flag);

    thorium_actor_send_reply_empty(self, ACTION_SET_VERTEX_FLAG_REPLY);
}

struct bsal_assembly_vertex *bsal_assembly_graph_store_find_vertex(struct thorium_actor *self,
                struct bsal_dna_kmer *kmer)
{
    struct bsal_memory_pool *ephemeral_memory;
    struct bsal_assembly_graph_store *concrete_self;
    char *sequence;
    struct bsal_dna_kmer storage_kmer;
    char *key;
    struct bsal_assembly_vertex *canonical_vertex;

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    concrete_self = thorium_actor_concrete_actor(self);

    sequence = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->kmer_length + 1);

    bsal_dna_kmer_get_sequence(kmer, sequence, concrete_self->kmer_length,
                        &concrete_self->transport_codec);

    bsal_dna_kmer_init(&storage_kmer, sequence, &concrete_self->storage_codec,
                        ephemeral_memory);

    key = bsal_memory_pool_allocate(ephemeral_memory, concrete_self->key_length_in_bytes);

    bsal_dna_kmer_pack_store_key(&storage_kmer, key,
                        concrete_self->kmer_length, &concrete_self->storage_codec,
                        ephemeral_memory);

    canonical_vertex = bsal_map_get(&concrete_self->table, key);

#ifdef BSAL_DEBUGGER_ASSERT
    if (canonical_vertex == NULL) {

        printf("not found Seq = %s name %d kmerlength %d key_length %d hash %" PRIu64 "\n", sequence,
                        thorium_actor_name(self),
                        concrete_self->kmer_length,
                        concrete_self->key_length_in_bytes,
                        bsal_dna_kmer_hash(&storage_kmer, concrete_self->kmer_length,
                                &concrete_self->storage_codec));
    }
#endif

    BSAL_DEBUGGER_ASSERT(canonical_vertex != NULL);

    bsal_memory_pool_free(ephemeral_memory, sequence);
    bsal_memory_pool_free(ephemeral_memory, key);
    bsal_dna_kmer_destroy(&storage_kmer, ephemeral_memory);

    return canonical_vertex;
}
