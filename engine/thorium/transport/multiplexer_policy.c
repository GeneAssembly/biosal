
#include "multiplexer_policy.h"

#include "message_multiplexer.h"

#include <core/helpers/set_helper.h>

#include <engine/thorium/node.h>
#include <engine/thorium/actor.h>

#include <biosal.h>

/*
 * Size threshold.
 */
#define THORIUM_MESSAGE_MULTIPLEXER_SIZE_THRESHOLD_IN_BYTES (0.90 * BIOSAL_IDEAL_BUFFER_SIZE)

/*
 * Time threshold in microseconds.
 *
 * There are 1 000 ms in 1 second
 * There are 1 000 000 us in 1 second.
 * There are 1 000 000 000 ns in 1 second.
 */
#define TIMEOUT_IN_MICRO_SECONDS 0
#define THORIUM_MESSAGE_MULTIPLEXER_TIME_THRESHOLD_IN_NANOSECONDS ( TIMEOUT_IN_MICRO_SECONDS * 100)

void thorium_multiplexer_policy_init(struct thorium_multiplexer_policy *self)
{
    self->threshold_buffer_size_in_bytes = THORIUM_MESSAGE_MULTIPLEXER_SIZE_THRESHOLD_IN_BYTES;
    /*self->threshold_time_in_nanoseconds = THORIUM_DYNAMIC_TIMEOUT;*/
    self->threshold_time_in_nanoseconds = THORIUM_MESSAGE_MULTIPLEXER_TIME_THRESHOLD_IN_NANOSECONDS;

    core_set_init(&self->actions_to_skip, sizeof(int));

    /*
     * We don't want to slow down things so the following actions
     * are not multiplexed.
     */

    core_set_add_int(&self->actions_to_skip, ACTION_MULTIPLEXER_MESSAGE);
    core_set_add_int(&self->actions_to_skip, ACTION_THORIUM_NODE_START);
    core_set_add_int(&self->actions_to_skip, ACTION_THORIUM_NODE_ADD_INITIAL_ACTOR);
    core_set_add_int(&self->actions_to_skip, ACTION_THORIUM_NODE_ADD_INITIAL_ACTORS);
    core_set_add_int(&self->actions_to_skip, ACTION_THORIUM_NODE_ADD_INITIAL_ACTORS_REPLY);
    core_set_add_int(&self->actions_to_skip, ACTION_SPAWN);
    core_set_add_int(&self->actions_to_skip, ACTION_SPAWN_REPLY);

    self->disabled = 0;

    /*
     * This is the minimum number of thorium nodes
     * needed for enabling the multiplexer.
     */
    self->minimum_node_count = 16;
}

void thorium_multiplexer_policy_destroy(struct thorium_multiplexer_policy *self)
{
    core_set_destroy(&self->actions_to_skip);

    self->threshold_buffer_size_in_bytes = -1;
    self->threshold_time_in_nanoseconds = -1;
}

int thorium_multiplexer_policy_is_action_to_skip(struct thorium_multiplexer_policy *self, int action)
{
    return core_set_find(&self->actions_to_skip, &action);
}

int thorium_multiplexer_policy_is_disabled(struct thorium_multiplexer_policy *self)
{
    return self->disabled;
}

int thorium_multiplexer_policy_size_threshold(struct thorium_multiplexer_policy *self)
{
    return self->threshold_buffer_size_in_bytes;
}

int thorium_multiplexer_policy_time_threshold(struct thorium_multiplexer_policy *self)
{
    return self->threshold_time_in_nanoseconds;
}

int thorium_multiplexer_policy_minimum_node_count(struct thorium_multiplexer_policy *self)
{
    return self->minimum_node_count;
}
