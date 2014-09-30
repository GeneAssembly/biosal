
#include "assembly_arc_classifier.h"

#include "assembly_arc_kernel.h"
#include "assembly_arc_block.h"

#include <genomics/kernels/dna_kmer_counter_kernel.h>

#include <core/system/debugger.h>

#include <stdio.h>

struct thorium_script bsal_assembly_arc_classifier_script = {
    .identifier = SCRIPT_ASSEMBLY_ARC_CLASSIFIER,
    .name = "bsal_assembly_arc_classifier",
    .init = bsal_assembly_arc_classifier_init,
    .destroy = bsal_assembly_arc_classifier_destroy,
    .receive = bsal_assembly_arc_classifier_receive,
    .size = sizeof(struct bsal_assembly_arc_classifier),
    .author = "Sebastien Boisvert",
    .description = "Arc classifier for metagenomics"
};

void bsal_assembly_arc_classifier_init(struct thorium_actor *self)
{
    struct bsal_assembly_arc_classifier *concrete_self;

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);

    concrete_self->kmer_length = -1;

    thorium_actor_add_action(self, ACTION_ASK_TO_STOP,
                    thorium_actor_ask_to_stop);

    thorium_actor_add_action(self, ACTION_SET_KMER_LENGTH,
                    bsal_assembly_arc_classifier_set_kmer_length);

    /*
     *
     * Configure the codec.
     */

    bsal_dna_codec_init(&concrete_self->codec);

    if (bsal_dna_codec_must_use_two_bit_encoding(&concrete_self->codec,
                            thorium_actor_get_node_count(self))) {
        bsal_dna_codec_enable_two_bit_encoding(&concrete_self->codec);
    }

    bsal_vector_init(&concrete_self->consumers, sizeof(int));

    thorium_actor_add_action(self, ACTION_ASSEMBLY_PUSH_ARC_BLOCK,
                    bsal_assembly_arc_classifier_push_arc_block);

    concrete_self->received_blocks = 0;

    bsal_vector_init(&concrete_self->pending_requests, sizeof(int));
    concrete_self->active_requests = 0;

    concrete_self->producer_is_waiting = 0;

    concrete_self->maximum_pending_request_count = thorium_actor_active_message_limit(self);

    concrete_self->consumer_count_above_threshold = 0;

    printf("%s/%d is now active, ACTIVE_MESSAGE_LIMIT %d\n",
                    thorium_actor_script_name(self),
                    thorium_actor_name(self),
                    concrete_self->maximum_pending_request_count);
}

void bsal_assembly_arc_classifier_destroy(struct thorium_actor *self)
{
    struct bsal_assembly_arc_classifier *concrete_self;

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);

    concrete_self->kmer_length = -1;

    bsal_vector_destroy(&concrete_self->consumers);

    bsal_dna_codec_destroy(&concrete_self->codec);

    bsal_vector_destroy(&concrete_self->pending_requests);
}

void bsal_assembly_arc_classifier_receive(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;
    void *buffer;
    struct bsal_assembly_arc_classifier *concrete_self;
    int size;
    int i;
    int *bucket;
    int source;
    int source_index;

    if (thorium_actor_take_action(self, message)) {
        return;
    }

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);
    tag = thorium_message_action(message);
    buffer = thorium_message_buffer(message);
    source = thorium_message_source(message);

    if (tag == ACTION_SET_CONSUMERS) {

        bsal_vector_unpack(&concrete_self->consumers, buffer);

        size = bsal_vector_size(&concrete_self->consumers);
        bsal_vector_resize(&concrete_self->pending_requests, size);

        for (i = 0; i < size; i++) {
            bsal_vector_set_int(&concrete_self->pending_requests, i, 0);
        }

        thorium_actor_send_reply_empty(self, ACTION_SET_CONSUMERS_REPLY);

    } else if (tag == ACTION_ASSEMBLY_PUSH_ARC_BLOCK_REPLY){

        /*
         * Decrease counter now.
         */
        source_index = bsal_vector_index_of(&concrete_self->consumers, &source);
        bucket = bsal_vector_at(&concrete_self->pending_requests, source_index);
        --(*bucket);
        --concrete_self->active_requests;

        /*
         * The previous value was maximum_pending_request_count + 1
         */
        if (*bucket == concrete_self->maximum_pending_request_count) {

            --concrete_self->consumer_count_above_threshold;
        }

        bsal_assembly_arc_classifier_verify_counters(self);
    }
}

void bsal_assembly_arc_classifier_set_kmer_length(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_arc_classifier *concrete_self;

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);

    thorium_message_unpack_int(message, 0, &concrete_self->kmer_length);

    thorium_actor_send_reply_empty(self, ACTION_SET_KMER_LENGTH_REPLY);
}

void bsal_assembly_arc_classifier_push_arc_block(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_assembly_arc_classifier *concrete_self;
    int source;
    struct bsal_assembly_arc_block input_block;
    struct bsal_assembly_arc_block *output_block;
    struct bsal_vector output_blocks;
    struct bsal_memory_pool *ephemeral_memory;
    int consumer_count;
    struct bsal_vector *input_arcs;
    struct bsal_vector *output_arcs;
    int size;
    int i;
    struct bsal_assembly_arc *arc;
    void *buffer;
    int count;
    struct bsal_dna_kmer *kmer;
    int consumer_index;
    int arc_count;
    int consumer;
    struct thorium_message new_message;
    int new_count;
    void *new_buffer;
    int *bucket;
    int maximum_pending_requests;
    int maximum_buffer_length;
    int reservation;

    count = thorium_message_count(message);
    buffer = thorium_message_buffer(message);

    if (count == 0) {
        printf("Error, count is 0 (classifier_push_arc_block)\n");
        return;
    }

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);
    source = thorium_message_source(message);
    consumer_count = bsal_vector_size(&concrete_self->consumers);
    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);

    BSAL_DEBUGGER_LEAK_DETECTION_BEGIN(ephemeral_memory, classify_arcs);

    bsal_vector_init(&output_blocks, sizeof(struct bsal_assembly_arc_block));
    bsal_vector_set_memory_pool(&output_blocks, ephemeral_memory);

    bsal_assembly_arc_block_init(&input_block, ephemeral_memory, concrete_self->kmer_length,
                    &concrete_self->codec);

#ifdef BSAL_ASSEMBLY_ARC_CLASSIFIER_DEBUG
    printf("UNPACKING\n");
#endif

    bsal_assembly_arc_block_unpack(&input_block, buffer, concrete_self->kmer_length,
                    &concrete_self->codec, ephemeral_memory);

#ifdef BSAL_ASSEMBLY_ARC_CLASSIFIER_DEBUG
    printf("OK\n");
#endif

    input_arcs = bsal_assembly_arc_block_get_arcs(&input_block);

    /*
     * Configure the ephemeral memory reservation.
     */
    arc_count = bsal_vector_size(input_arcs);
    reservation = (arc_count / consumer_count) * 2;

    bsal_vector_resize(&output_blocks, consumer_count);

    BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));

    /*
     * Initialize output blocks.
     * There is one for each destination.
     */
    for (i = 0; i < consumer_count; i++) {

        output_block = bsal_vector_at(&output_blocks, i);

        bsal_assembly_arc_block_init(output_block, ephemeral_memory, concrete_self->kmer_length,
                        &concrete_self->codec);

        bsal_assembly_arc_block_reserve(output_block, reservation);
    }

    size = bsal_vector_size(input_arcs);

    /*
     * Classify every arc in the input block
     * and put them in output blocks.
     */

#ifdef BSAL_ASSEMBLY_ARC_CLASSIFIER_DEBUG
    printf("ClassifyArcs arc_count= %d\n", size);

#endif

    BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));

    for (i = 0; i < size; i++) {

        arc = bsal_vector_at(input_arcs, i);

        kmer = bsal_assembly_arc_source(arc);

        consumer_index = bsal_dna_kmer_store_index(kmer, consumer_count,
                        concrete_self->kmer_length, &concrete_self->codec,
                        ephemeral_memory);

        output_block = bsal_vector_at(&output_blocks, consumer_index);

        /*
         * Make a copy of the arc and copy it.
         * It will be freed
         */

        bsal_assembly_arc_block_add_arc_copy(output_block, arc,
                        concrete_self->kmer_length, &concrete_self->codec,
                        ephemeral_memory);
    }

    /*
     * Input arcs are not needed anymore.
     */
    bsal_assembly_arc_block_destroy(&input_block, ephemeral_memory);

    BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));

    /*
     * Finally, send these output blocks to consumers.
     */

    maximum_pending_requests = 0;
    maximum_buffer_length = 0;

    /*
     * Figure out the maximum buffer length tor
     * messages.
     */
    for (i = 0; i < consumer_count; i++) {

        output_block = bsal_vector_at(&output_blocks, i);
        new_count = bsal_assembly_arc_block_pack_size(output_block, concrete_self->kmer_length,
                    &concrete_self->codec);

        if (new_count > maximum_buffer_length) {
            maximum_buffer_length = new_count;
        }
    }

#if 0
    printf("POOL_BALANCE %d\n",
                    bsal_memory_pool_profile_balance_count(ephemeral_memory));
#endif

    for (i = 0; i < consumer_count; i++) {

        output_block = bsal_vector_at(&output_blocks, i);
        output_arcs = bsal_assembly_arc_block_get_arcs(output_block);
        arc_count = bsal_vector_size(output_arcs);

        /*
         * Don't send an empty message.
         */
        if (arc_count > 0) {

            /*
             * Allocation is not required because new_count <= maximum_buffer_length
             */
            new_count = bsal_assembly_arc_block_pack_size(output_block, concrete_self->kmer_length,
                    &concrete_self->codec);

            new_buffer = thorium_actor_allocate(self, maximum_buffer_length);

            BSAL_DEBUGGER_ASSERT(new_count <= maximum_buffer_length);

            bsal_assembly_arc_block_pack(output_block, new_buffer, concrete_self->kmer_length,
                    &concrete_self->codec);

            thorium_message_init(&new_message, ACTION_ASSEMBLY_PUSH_ARC_BLOCK,
                    new_count, new_buffer);

            consumer = bsal_vector_at_as_int(&concrete_self->consumers, i);

            /*
             * Send the message.
             */
            thorium_actor_send(self, consumer, &new_message);
            thorium_message_destroy(&new_message);

            /* update event counters for control.
             */
            bucket = bsal_vector_at(&concrete_self->pending_requests, i);
            ++(*bucket);
            ++concrete_self->active_requests;

            if (*bucket > maximum_pending_requests) {
                maximum_pending_requests = *bucket;
            }

            if (*bucket > concrete_self->maximum_pending_request_count) {
                ++concrete_self->consumer_count_above_threshold;
            }
        }

        BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));

#if 0
        printf("i = %d\n", i);
#endif

        /*
         * Destroy output block.
         */
        bsal_assembly_arc_block_destroy(output_block,
                    ephemeral_memory);

        BSAL_DEBUGGER_LEAK_CHECK_DOUBLE_FREE(ephemeral_memory);
        BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));
    }

    bsal_vector_destroy(&output_blocks);

    BSAL_DEBUGGER_ASSERT(!bsal_memory_pool_has_double_free(ephemeral_memory));

    BSAL_DEBUGGER_LEAK_CHECK_DOUBLE_FREE(ephemeral_memory);

    /*
     * Check if a response must be sent now.
     */

    ++concrete_self->received_blocks;
    concrete_self->source = source;

    /*
     * Only send a direct reply if there is enough memory.
     *
     * As long as maximum_pending_requests is lower than maximum_pending_request_count,
     * there is still space for at least one additional request.
     */
    if (maximum_pending_requests < concrete_self->maximum_pending_request_count
            && bsal_memory_has_enough_bytes()) {

        thorium_actor_send_empty(self, concrete_self->source,
                    ACTION_ASSEMBLY_PUSH_ARC_BLOCK_REPLY);
    } else {

        concrete_self->producer_is_waiting = 1;
    }

    BSAL_DEBUGGER_LEAK_DETECTION_END(ephemeral_memory, classify_arcs);
}

void bsal_assembly_arc_classifier_verify_counters(struct thorium_actor *self)
{
    struct bsal_assembly_arc_classifier *concrete_self;

    concrete_self = (struct bsal_assembly_arc_classifier *)thorium_actor_concrete_actor(self);

#if 0
    size = bsal_vector_size(&concrete_self->consumers);
#endif

    /*
     * Don't do anything if the producer is not waiting anyway.
     */
    if (!concrete_self->producer_is_waiting) {
        return;
    }

    if (concrete_self->consumer_count_above_threshold > 0) {
        return;
    }

    /*
     * Make sure that we have enough memory available.
     * This verification is not performed if there are 0 active
     * requests.
     */
    /*
     * The code here is to make sure that there is enough memory.
     */

    if (concrete_self->active_requests > 0
            && !bsal_memory_has_enough_bytes()) {
        return;
    }

#if 0
    /*
     * Abort if at least one counter is above the threshold.
     */
    for (i = 0; i < size; i++) {
        bucket = bsal_vector_at(&concrete_self->pending_requests, i);
        active_count = *bucket;

        if (active_count > concrete_self->maximum_pending_request_count) {

            return;
        }
    }
#endif

    /*
     * Trigger an actor event now.
     */
    thorium_actor_send_empty(self, concrete_self->source,
             ACTION_ASSEMBLY_PUSH_ARC_BLOCK_REPLY);

    concrete_self->producer_is_waiting = 0;
}
