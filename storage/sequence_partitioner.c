
#include "sequence_partitioner.h"

#include "stream_command.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <inttypes.h>

/*
#define BSAL_SEQUENCE_PARTITIONER_DEBUG
*/

struct bsal_script bsal_sequence_partitioner_script = {
    .name = BSAL_SEQUENCE_PARTITIONER_SCRIPT,
    .init = bsal_sequence_partitioner_init,
    .destroy = bsal_sequence_partitioner_destroy,
    .receive = bsal_sequence_partitioner_receive,
    .size = sizeof(struct bsal_sequence_partitioner)
};

void bsal_sequence_partitioner_init(struct bsal_actor *actor)
{
    struct bsal_sequence_partitioner *concrete_actor;

    concrete_actor = (struct bsal_sequence_partitioner *)bsal_actor_concrete_actor(actor);

    bsal_vector_init(&concrete_actor->stream_entries, sizeof(uint64_t));
    bsal_vector_init(&concrete_actor->stream_positions, sizeof(uint64_t));
    bsal_vector_init(&concrete_actor->stream_global_positions, sizeof(uint64_t));

    bsal_queue_init(&concrete_actor->available_commands, sizeof(struct bsal_stream_command));
    bsal_dynamic_hash_table_init(&concrete_actor->active_commands, 128, sizeof(int),
                    sizeof(struct bsal_stream_command));

    concrete_actor->store_count = -1;
    concrete_actor->block_size = -1;

    concrete_actor->command_number = 0;
}

void bsal_sequence_partitioner_destroy(struct bsal_actor *actor)
{
    struct bsal_sequence_partitioner *concrete_actor;

    concrete_actor = (struct bsal_sequence_partitioner *)bsal_actor_concrete_actor(actor);

    bsal_vector_destroy(&concrete_actor->stream_entries);
    bsal_vector_destroy(&concrete_actor->stream_positions);
    bsal_vector_destroy(&concrete_actor->stream_global_positions);

    bsal_queue_destroy(&concrete_actor->available_commands);
    bsal_dynamic_hash_table_destroy(&concrete_actor->active_commands);
}

void bsal_sequence_partitioner_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    int count;
    void *buffer;
    int bytes;
    struct bsal_sequence_partitioner *concrete_actor;
    struct bsal_stream_command command;
    struct bsal_message response;
    int command_number;
    struct bsal_stream_command *active_command;
    int stream_index;
    struct bsal_stream_command *command_bucket;

    bsal_message_get_all(message, &tag, &count, &buffer, &source);

    concrete_actor = (struct bsal_sequence_partitioner *)bsal_actor_concrete_actor(actor);

    if (tag == BSAL_SEQUENCE_PARTITIONER_SET_BLOCK_SIZE) {
        bsal_message_unpack_int(message, 0, &concrete_actor->block_size);
        bsal_actor_send_reply_empty(actor, BSAL_SEQUENCE_PARTITIONER_SET_BLOCK_SIZE_REPLY);

        bsal_sequence_partitioner_verify(actor);
/*
        printf("DEBUG bsal_sequence_partitioner_receive received block size\n");
        */
    } else if (tag == BSAL_SEQUENCE_PARTITIONER_SET_ENTRY_VECTOR) {

            /*
        printf("DEBUG bsal_sequence_partitioner_receive unpacking vector, %d bytes\n",
                        count);
                        */

        bsal_vector_unpack(&concrete_actor->stream_entries, buffer);

        /*
        printf("DEBUG after unpack\n");
        */

        bsal_actor_send_reply_empty(actor, BSAL_SEQUENCE_PARTITIONER_SET_ENTRY_VECTOR_REPLY);

        /*
        printf("DEBUG bsal_sequence_partitioner_receive received received entry vector\n");
        */
        bsal_sequence_partitioner_verify(actor);

    } else if (tag == BSAL_SEQUENCE_PARTITIONER_SET_ACTOR_COUNT) {

        bsal_message_unpack_int(message, 0, &concrete_actor->store_count);
        bsal_actor_send_reply_empty(actor, BSAL_SEQUENCE_PARTITIONER_SET_ACTOR_COUNT_REPLY);

        bsal_sequence_partitioner_verify(actor);
        /*
        printf("DEBUG bsal_sequence_partitioner_receive received received store count\n");
        */

    } else if (tag == BSAL_SEQUENCE_PARTITIONER_GET_COMMAND) {

        if (bsal_queue_dequeue(&concrete_actor->available_commands, &command)) {

                /*
            printf("DEBUG partitioner has command !\n");
            */
            bytes = bsal_stream_command_pack_size(&command);
            buffer = malloc(bytes);
            bsal_stream_command_pack(&command, buffer);

            bsal_message_init(&response, BSAL_SEQUENCE_PARTITIONER_GET_COMMAND_REPLY,
                            bytes, buffer);
            bsal_actor_send_reply(actor, &response);

            /* store the active command
             */
            command_number = bsal_stream_command_name(&command);
            command_bucket = (struct bsal_stream_command *)bsal_dynamic_hash_table_add(&concrete_actor->active_commands,
                            &command_number);
            *command_bucket = command;

            free(buffer);

            /* there may be other command available too !
             */
        }

    } else if (tag == BSAL_SEQUENCE_PARTITIONER_GET_COMMAND_REPLY_REPLY) {
        /*
         * take the name of the command, find it in the active
         * command, generate a new command, and send BSAL_SEQUENCE_PARTITIONER_COMMAND_IS_READY
         * as a reply
         */

        bsal_message_unpack_int(message, 0, &command_number);

        active_command = bsal_dynamic_hash_table_get(&concrete_actor->active_commands,
                        &command_number);

        if (active_command == NULL) {
            return;
        }

        stream_index = bsal_stream_command_stream_index(active_command);
        active_command = NULL;
        bsal_dynamic_hash_table_delete(&concrete_actor->active_commands,
                        &command_number);

        bsal_sequence_partitioner_generate_command(actor, stream_index);

        if (bsal_dynamic_hash_table_size(&concrete_actor->active_commands) == 0
                        && bsal_queue_size(&concrete_actor->available_commands) == 0) {

            bsal_actor_send_reply_empty(actor, BSAL_SEQUENCE_PARTITIONER_FINISHED);
        }

    } else if (tag == BSAL_ACTOR_ASK_TO_STOP
                    && source == bsal_actor_supervisor(actor)) {

#ifdef BSAL_SEQUENCE_PARTITIONER_DEBUG
        printf("DEBUG bsal_sequence_partitioner_receive BSAL_ACTOR_ASK_TO_STOP\n");
#endif

        bsal_actor_send_to_self_empty(actor,
                        BSAL_ACTOR_STOP);
    }
}

void bsal_sequence_partitioner_verify(struct bsal_actor *actor)
{
    struct bsal_sequence_partitioner *concrete_actor;
    int i;
    uint64_t entries;
    int position;
    uint64_t stream_entries;

    concrete_actor = (struct bsal_sequence_partitioner *)bsal_actor_concrete_actor(actor);

    if (concrete_actor->block_size == -1) {
        return;
    }

    if (concrete_actor->store_count == -1) {
        return;
    }

    if (bsal_vector_size(&concrete_actor->stream_entries) == 0) {
        return;
    }

    /* prepare <stream_entries.size> commands
     */

    position = 0;

    entries = 0;

    /*
    printf("DEBUG generating initial positions\n");
    */
    /* generate stream positions, stream global positions, and total
     */
    for (i = 0; i < bsal_vector_size(&concrete_actor->stream_entries); i++) {

        bsal_vector_push_back(&concrete_actor->stream_positions, &position);

        bsal_vector_push_back(&concrete_actor->stream_global_positions, &entries);

        stream_entries = *(uint64_t *)bsal_vector_at(&concrete_actor->stream_entries, i);

        entries += stream_entries;
    }

    concrete_actor->total = entries;

    /*
    printf("DEBUG entries= %" PRIu64 "\n", entries);
    printf("DEBUG generating commands \n");
*/
    /* generate commands
     */
    for (i = 0; i < bsal_vector_size(&concrete_actor->stream_entries); i++) {

            /*
        printf("DEBUG %d...\n", i);
        */
        bsal_sequence_partitioner_generate_command(actor, i);
    }
}

int bsal_sequence_partitioner_get_store(int block_size, int store_count, uint64_t index)
{
    /*
     * idea:
     *
     * first          ... last                 ........ store
     *
     * 0 * block_size ... 1 * block_size - 1   ........ store 0
     * 1 * block_size ... 2 * block_size - 1   ........ store 1
     * 2 * block_size ... 3 * block_size - 1   ........ store 2
     * 3 * block_size ... 3 * block_size - 1   ........ store 3
     * ...
     *
     * pattern:
     *
     * x * block_size ... (x + 1) * block_size - 1   ........ store (x % store_count)
     *
     *
     * Given an index i and block_size b, we have:
     *
     * (x * b) <= i <= ( (x + 1) * b - 1 )
     *
     * (x * b) <= i
     * x <= i / b (Equation 1.)
     *
     * i <= ( (x + 1) * b - 1 )
     * i <= (x + 1) * b - 1
     * i + 1 <= (x + 1) * b
     * ( i + 1 ) / b <= (x + 1)
     * ( i + 1 ) / b <= x + 1
     * ( i + 1 ) / b - 1 <= x (Equation 2.)
     *
     * So, dividing index by block_size gives x.
     */

    /* do the calculation in 64 bits to be sure.
     */

    if (store_count == 0) {
        return 0;
    }

    return ( index / (uint64_t) block_size) % ( (uint64_t) store_count);
}

void bsal_sequence_partitioner_generate_command(struct bsal_actor *actor, int stream_index)
{
    uint64_t *bucket_for_position;
    uint64_t *bucket_for_global_position;
    struct bsal_stream_command command;
    int first;
    int last;
    int store_index;
    uint64_t global_first;
    uint64_t global_last;
    uint64_t stream_entries;
    struct bsal_sequence_partitioner *concrete_actor;

    concrete_actor = (struct bsal_sequence_partitioner *)bsal_actor_concrete_actor(actor);

    /*
    printf("DEBUG bsal_sequence_partitioner_generate_command %d\n",
                    stream_index);
*/

    bucket_for_position = (uint64_t *)bsal_vector_at(&concrete_actor->stream_positions, stream_index);
    bucket_for_global_position = (uint64_t *)bsal_vector_at(&concrete_actor->stream_global_positions,
                    stream_index);

    /*
    printf("DEBUG got buckets.\n");
*/
    first = *bucket_for_position;
    stream_entries = *(uint64_t *)bsal_vector_at(&concrete_actor->stream_entries, stream_index);

    if (stream_entries == 0) {
        return;
    }

    /*
    printf("DEBUG bsal_sequence_partitioner_generate_command stream %d entries %" PRIu64 " first %d\n",
                    stream_index, stream_entries, first);
*/

    if (first > stream_entries - 1) {
        return;
    }

    last = first + concrete_actor->block_size - 1;

    if (last > stream_entries - 1) {
        last = stream_entries - 1;
    }

    global_first = *bucket_for_global_position;

    if (global_first > concrete_actor->total - 1) {
        return;
    }

    global_last = global_first + concrete_actor->block_size - 1;

    if (global_last > concrete_actor->total - 1) {
        global_last = concrete_actor->total - 1;
    }

    store_index = bsal_sequence_partitioner_get_store(concrete_actor->block_size,
                    concrete_actor->store_count, global_first);
    /*
    printf("DEBUG %" PRIu64 " goes in store %d\n",
                    global_first, store_index);
*/
    bsal_stream_command_init(&command, concrete_actor->command_number,
                    stream_index, first, last, global_first,
                    global_last, store_index);
    concrete_actor->command_number++;

    bsal_queue_enqueue(&concrete_actor->available_commands,
                    &command);
    /*
    bsal_stream_command_print(&command);
    */

    bsal_stream_command_destroy(&command);

    /* update positions
     */
    *bucket_for_position = last + 1;
    *bucket_for_global_position = global_last + 1;

    /*
    printf("DEBUG command is ready\n");
    */

    /* emit a signal
     */
    bsal_actor_send_reply_empty(actor, BSAL_SEQUENCE_PARTITIONER_COMMAND_IS_READY);
}
