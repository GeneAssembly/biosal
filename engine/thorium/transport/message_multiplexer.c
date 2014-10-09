
#include "message_multiplexer.h"
#include "multiplexed_buffer.h"

#include <engine/thorium/node.h>

#include <core/helpers/bitmap.h>

#include <core/system/debugger.h>
#include <core/system/memory.h>

#include <core/structures/vector.h>
#include <core/helpers/set_helper.h>
#include <core/structures/set_iterator.h>

#include <stdlib.h>

#define FORCE_NO 0
#define FORCE_YES_SIZE 1
#define FORCE_YES_TIME 1

#define FLAG_DISABLED 0

/*
#define DEBUG_MULTIPLEXER
*/

#define MEMORY_MULTIPLEXER 0xb606aa9d

/*
 * Internal function for flushing stuff away.
 */
void thorium_message_multiplexer_flush(struct thorium_message_multiplexer *self, int index, int force);


void thorium_message_multiplexer_init(struct thorium_message_multiplexer *self,
                struct thorium_node *node, struct thorium_multiplexer_policy *policy)
{
    int size;
    int i;
    int bytes;
    int position;
    struct thorium_multiplexed_buffer *multiplexed_buffer;

    self->policy = policy;
    self->original_message_count = 0;
    self->real_message_count = 0;

    CORE_BITMAP_CLEAR(self->flags);
    CORE_BITMAP_CLEAR_BIT(self->flags, FLAG_DISABLED);

    core_set_init(&self->buffers_with_content, sizeof(int));

    core_timer_init(&self->timer);

    self->buffer_size_in_bytes = thorium_multiplexer_policy_size_threshold(self->policy);
    self->timeout_in_nanoseconds = thorium_multiplexer_policy_time_threshold(self->policy);

    self->node = node;

    core_vector_init(&self->buffers, sizeof(struct thorium_multiplexed_buffer));

    size = thorium_node_nodes(self->node);
    core_vector_resize(&self->buffers, size);

    bytes = size * self->buffer_size_in_bytes;

#ifdef DEBUG_MULTIPLEXER
    printf("DEBUG_MULTIPLEXER size %d bytes %d\n", size, bytes);
#endif

    self->big_buffer = core_memory_allocate(bytes, MEMORY_MULTIPLEXER);
    position = 0;

    for (i = 0; i < size; ++i) {
        multiplexed_buffer = core_vector_at(&self->buffers, i);

        multiplexed_buffer->buffer = self->big_buffer + position;
        position += self->buffer_size_in_bytes;

#ifdef DEBUG_MULTIPLEXER
        printf("DEBUG_MULTIPLEXER thorium_message_multiplexer_init index %d buffer %p\n", i, buffer);
#endif

#ifdef DEBUG_MULTIPLEXER
        printf("DEBUG_MULTIPLEXER thorium_message_multiplexer_init (after) index %d buffer %p\n", i,
                        core_vector_at(&self->buffers, i));
#endif

        multiplexed_buffer->current_size = 0;
        multiplexed_buffer->message_count = 0;
        multiplexed_buffer->maximum_size = self->buffer_size_in_bytes;
    }

    self->last_flush = core_timer_get_nanoseconds(&self->timer);

    if (thorium_multiplexer_policy_is_disabled(self->policy)) {
        CORE_BITMAP_SET_BIT(self->flags, FLAG_DISABLED);
    }

    if (thorium_node_nodes(self->node) < thorium_multiplexer_policy_minimum_node_count(self->policy)) {
        CORE_BITMAP_SET_BIT(self->flags, FLAG_DISABLED);
    }

    if (thorium_node_name(self->node) == 0) {
        if (self->timeout_in_nanoseconds == THORIUM_DYNAMIC_TIMEOUT) {
            printf("thorium_message_multiplexer: disabled=%d buffer_size_in_bytes=%d timeout_in_nanoseconds=dynamic\n",
                            CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED),
                        self->buffer_size_in_bytes);
        } else {
            printf("thorium_message_multiplexer: disabled=%d buffer_size_in_bytes=%d timeout_in_nanoseconds=%d\n",
                            CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED),
                        self->buffer_size_in_bytes, self->timeout_in_nanoseconds);
        }
    }
}

void thorium_message_multiplexer_destroy(struct thorium_message_multiplexer *self)
{
    int i;
    int size;
    struct thorium_multiplexed_buffer *multiplexed_buffer;
    float ratio;

    ratio = 0.0;

    if (self->original_message_count != 0) {
        ratio = self->real_message_count / (0.0 + self->original_message_count);
    }
    printf("thorium_message_multiplexer: original_message_count %d real_message_count %d (%.4f)\n",
                    self->original_message_count, self->real_message_count, ratio);

#ifdef CORE_DEBUGGER_ENABLE_ASSERT
#endif
    size = core_vector_size(&self->buffers);

    CORE_DEBUGGER_ASSERT(core_set_empty(&self->buffers_with_content));

    core_set_destroy(&self->buffers_with_content);

    for (i = 0; i < size; ++i) {
        multiplexed_buffer = core_vector_at(&self->buffers, i);

        CORE_DEBUGGER_ASSERT(multiplexed_buffer->current_size == 0);

        multiplexed_buffer->buffer = 0;
    }

    core_vector_destroy(&self->buffers);

    self->node = NULL;

    self->buffer_size_in_bytes = -1;
    self->timeout_in_nanoseconds = -1;

    core_memory_free(self->big_buffer, MEMORY_MULTIPLEXER);
    self->big_buffer = NULL;

    self->last_flush = 0;

    core_timer_destroy(&self->timer);
}

/*
 * Returns 1 if the message was multiplexed.
 *
 * This is O(1) in regard to the number of thorium nodes.
 */
int thorium_message_multiplexer_multiplex(struct thorium_message_multiplexer *self,
                struct thorium_message *message)
{
    /*
     * If buffer is full, use thorium_node_send_with_transport
     *
     * get count
     *
     * if count is below or equal to the threshold
     *      multiplex the message.
     *      return 1
     *
     * return 0
     */

    int count;
    int current_size;
    int maximum_size;
    int action;
    void *buffer;
    int destination_node;
    int destination_actor;
    int new_size;
    void *multiplexed_buffer;
    void *destination_in_buffer;
    int required_size;
    struct thorium_multiplexed_buffer *real_multiplexed_buffer;

    ++self->original_message_count;

    if (CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED)) {
        ++self->real_message_count;
        return 0;
    }

    action = thorium_message_action(message);

    /*
     * Don't multiplex already-multiplexed messages.
     */
    if (thorium_multiplexer_policy_is_action_to_skip(self->policy, action)) {
        ++self->real_message_count;
        return 0;
    }

    count = thorium_message_count(message);
    required_size = sizeof(count) + count;
    buffer = thorium_message_buffer(message);
    destination_actor = thorium_message_destination(message);

    /*
     * Don't multiplex non-actor messages.
     */
    if (destination_actor == THORIUM_ACTOR_NOBODY) {
        return 0;
    }

    destination_node = thorium_message_destination_node(message);

    real_multiplexed_buffer = core_vector_at(&self->buffers, destination_node);

#ifdef CORE_DEBUGGER_ASSERT
    if (real_multiplexed_buffer == NULL) {
        printf("Error action %d destination_node %d destination_actor %d\n", action, destination_node,
                        destination_actor);
    }
#endif

    CORE_DEBUGGER_ASSERT(real_multiplexed_buffer != NULL);

    current_size = real_multiplexed_buffer->current_size;
    maximum_size = real_multiplexed_buffer->maximum_size;

    /*
     * Don't multiplex large messages.
     */
    if (required_size > maximum_size) {

#ifdef DEBUG_MULTIPLEXER
        printf("too large required_size %d maximum_size %d\n", required_size, maximum_size);
#endif
        return 0;
    }

    new_size = current_size + required_size;

    /*
     * Flush now if there is no space left for the <required_size> bytes
     */
    if (new_size > maximum_size) {

#ifdef DEBUG_MULTIPLEXER
        printf("thorium_message_multiplexer: must FLUSH thorium_message_multiplexer_multiplex required_size %d new_size %d maximum_size %d\n",
                    required_size, new_size, maximum_size);
#endif



        thorium_message_multiplexer_flush(self, destination_node, FORCE_YES_SIZE);
        current_size = real_multiplexed_buffer->current_size;

        CORE_DEBUGGER_ASSERT(current_size == 0);
    }

    multiplexed_buffer = real_multiplexed_buffer->buffer;
    destination_in_buffer = ((char *)multiplexed_buffer) + current_size;

    /*
     * Append <count><buffer> to the <multiplexed_buffer>
     */
    core_memory_copy(destination_in_buffer, &count, sizeof(count));
    core_memory_copy((char *)destination_in_buffer + sizeof(count),
                    buffer, count);

    current_size += required_size;

    CORE_DEBUGGER_ASSERT(current_size <= maximum_size);
    real_multiplexed_buffer->current_size = current_size;
    ++real_multiplexed_buffer->message_count;

    thorium_message_multiplexer_flush(self, destination_node, FORCE_NO);

    current_size = real_multiplexed_buffer->current_size;

    CORE_DEBUGGER_ASSERT(current_size <= maximum_size);

    /*
     * Add the key for this buffer with content.
     */
    if (current_size > 0) {
        core_set_add(&self->buffers_with_content, &destination_node);
    }

    /*
     * Inject the buffer into the worker too.
     */
    return 1;
}

/*
 * Returns 1 if the message was demultiplexed.
 *
 * This is O(1) in regard to the number of thorium nodes.
 */
int thorium_message_multiplexer_demultiplex(struct thorium_message_multiplexer *self,
                struct thorium_message *message)
{
    /*
     * Algorithm:
     *
     * get tag.
     * if tag is ACTION_MULTIPLEXER_MESSAGE
     *     for every enclosed message
     *         call thorium_node_prepare_received_message
     *         call thorium_node_dispatch_message()
     *     return 1
     *
     * return 0
     */

    int count;
    char *buffer;
    struct thorium_message new_message;
    int new_count;
    void *new_buffer;
    int position;
    struct core_memory_pool *pool;
    int messages;
    int tag;
    int source_node;
    int destination_node;

    if (CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED)) {
        return 0;
    }

    tag = thorium_message_action(message);

    if (tag != ACTION_MULTIPLEXER_MESSAGE) {
        return 0;
    }

    source_node = thorium_message_source_node(message);
    destination_node = thorium_message_destination_node(message);
    count = thorium_message_count(message);
    buffer = thorium_message_buffer(message);
    pool = thorium_node_inbound_memory_pool(self->node);

    position = 0;
    messages = 0;

    /*
     * Inject a message for each enclosed message.
     */
    while (position < count) {
        core_memory_copy(&new_count, buffer + position, sizeof(new_count));
        position += sizeof(new_count);

        new_buffer = core_memory_pool_allocate(pool, new_count);
        core_memory_copy(new_buffer, buffer + position, new_count);

        thorium_message_init_with_nodes(&new_message, new_count, new_buffer,
                        source_node, destination_node);

        thorium_node_prepare_received_message(self->node, &new_message);
        thorium_node_dispatch_message(self->node, &new_message);

        thorium_message_destroy(&new_message);

        position += new_count;
        ++messages;
    }

    CORE_DEBUGGER_ASSERT(messages > 0);

#ifdef DEBUG_MULTIPLEXER
    printf("thorium_message_multiplexer_demultiplex %d messages\n",
                    messages);
#endif

    return 1;
}

/*
 * This is O(n), that is if all thorium nodes have a buffer to flush.
 */
void thorium_message_multiplexer_test(struct thorium_message_multiplexer *self)
{
    /*
     * Check if the multiplexer has waited enough.
     */

    uint64_t time;
    struct core_set_iterator iterator;
    int duration;
    int index;

    if (CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED)) {
        return;
    }

    time = core_timer_get_nanoseconds(&self->timer);

    duration = time - self->last_flush;

    if (duration < self->timeout_in_nanoseconds) {
        return;
    }

    if (core_set_empty(&self->buffers_with_content)) {
        return;
    }

    core_set_iterator_init(&iterator, &self->buffers_with_content);

    while (core_set_iterator_get_next_value(&iterator, &index)) {

        thorium_message_multiplexer_flush(self, index, FORCE_YES_TIME);
    }

    core_set_iterator_destroy(&iterator);

    core_set_clear(&self->buffers_with_content);

    /*
     * Update the counter for last flush event.
     */
    self->last_flush = core_timer_get_nanoseconds(&self->timer);
}

void thorium_message_multiplexer_flush(struct thorium_message_multiplexer *self, int index, int force)
{
    char *buffer;
    struct thorium_message message;
    int tag;
    int count;
    int current_size;
    int maximum_size;
    struct thorium_multiplexed_buffer *multiplexed_buffer;

    if (CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED)) {
        return;
    }

    multiplexed_buffer = core_vector_at(&self->buffers, index);
    current_size = multiplexed_buffer->current_size;
    maximum_size = multiplexed_buffer->maximum_size;

    if (force == FORCE_NO && current_size < maximum_size) {
        return;
    }

    count = current_size;
    tag = ACTION_MULTIPLEXER_MESSAGE;
    buffer = multiplexed_buffer->buffer;

#ifdef DEBUG_MULTIPLEXER
    printf("DEBUG_MULTIPLEXER thorium_message_multiplexer_flush index %d buffer %p force %d message_count %d current_size %d maximum_size %d\n",
                    index, buffer, force, multiplexed_buffer->message_count,
                    current_size, maximum_size);
#endif

    thorium_message_init(&message, tag, count, buffer);
    thorium_node_send_to_node(self->node, index, &message);

    ++self->real_message_count;
    thorium_message_destroy(&message);

    multiplexed_buffer->current_size = 0;
    multiplexed_buffer->message_count = 0;

    core_set_delete(&self->buffers_with_content, &index);
}

int thorium_message_multiplexer_is_disabled(struct thorium_message_multiplexer *self)
{
    return CORE_BITMAP_GET_BIT(self->flags, FLAG_DISABLED);
}

