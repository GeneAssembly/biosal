
#include "actor.h"

#include "worker.h"
#include "node.h"

#include "scheduler/scheduling_queue.h"

#include <core/structures/vector_iterator.h>
#include <core/structures/map_iterator.h>

#include <core/helpers/vector_helper.h>
#include <core/helpers/message_helper.h>
#include <core/helpers/bitmap.h>

#include <core/system/memory.h>
#include <core/system/debugger.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Debugging options
 */

/*
#define THORIUM_ACTOR_DEBUG

#define THORIUM_ACTOR_DEBUG1

#define THORIUM_ACTOR_DEBUG_SYNC
#define THORIUM_ACTOR_DEBUG_BINOMIAL_TREE

#define THORIUM_ACTOR_DEBUG_SPAWN
#define THORIUM_ACTOR_DEBUG_10335

#define THORIUM_ACTOR_DEBUG_MIGRATE
#define THORIUM_ACTOR_DEBUG_FORWARDING
#define THORIUM_ACTOR_DEBUG_CLONE
*/

/* some constants
 */
#define THORIUM_ACTOR_ACQUAINTANCE_SUPERVISOR 0

#define THORIUM_ACTOR_FORWARDING_NONE 0
#define THORIUM_ACTOR_FORWARDING_CLONE 1
#define THORIUM_ACTOR_FORWARDING_MIGRATE 2

/*
 * Flags.
 */
#define FLAG_DEAD 0
#define FLAG_CAN_PACK 1
#define FLAG_MIGRATION_PROGRESSED 2
#define FLAG_LOCKED 3
#define FLAG_MIGRATION_CLONED 4
#define FLAG_MIGRATION_FORWARDED_MESSAGES 5
#define FLAG_CLONING_PROGRESSED 6
#define FLAG_SYNCHRONIZATION_STARTED 7

void thorium_actor_init(struct thorium_actor *self, void *concrete_actor,
                struct thorium_script *script, int name, struct thorium_node *node)
{
    thorium_actor_init_fn_t init;
    int capacity;

    thorium_actor_set_priority(self, THORIUM_PRIORITY_NORMAL);

    bsal_map_init(&self->received_messages, sizeof(int), sizeof(int));
    bsal_map_init(&self->sent_messages, sizeof(int), sizeof(int));

    /* initialize the dispatcher before calling
     * the concrete initializer
     */
    thorium_dispatcher_init(&self->dispatcher);

    self->concrete_actor = concrete_actor;
    self->name = name;
    self->node = node;

    self->flags = 0;

    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_DEAD);

    self->script = script;
    self->worker = NULL;

    self->spawner_index = THORIUM_ACTOR_NO_VALUE;

    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_SYNCHRONIZATION_STARTED);
    self->synchronization_expected_responses = 0;
    self->synchronization_responses = 0;

    bsal_lock_init(&self->receive_lock);
    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_LOCKED);

    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_CAN_PACK);

    self->cloning_status = THORIUM_ACTOR_STATUS_NOT_STARTED;
    self->migration_status = THORIUM_ACTOR_STATUS_NOT_STARTED;
    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_MIGRATION_CLONED);
    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_MIGRATION_FORWARDED_MESSAGES);

/*
*/
#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_vector_init(&self->acquaintance_vector, sizeof(int));
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_vector_init(&self->children, sizeof(int));
#endif

    bsal_queue_init(&self->queued_messages_for_clone, sizeof(struct thorium_message));
    bsal_queue_init(&self->queued_messages_for_migration, sizeof(struct thorium_message));
    bsal_queue_init(&self->forwarding_queue, sizeof(struct thorium_message));

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_map_init(&self->acquaintance_map, sizeof(int), sizeof(int));
#endif

    /*
    thorium_actor_send_to_self_empty(self, ACTION_UNPIN_FROM_WORKER);
    thorium_actor_send_to_self_empty(self, ACTION_PIN_TO_NODE);
    */

    bsal_queue_init(&self->enqueued_messages, sizeof(struct thorium_message));

    capacity = THORIUM_ACTOR_MAILBOX_SIZE;
    bsal_fast_ring_init(&self->mailbox, capacity, sizeof(struct thorium_message));

    /* call the concrete initializer
     * this must be the last call.
     */
    init = thorium_actor_get_init(self);
    init(self);

    BSAL_DEBUGGER_ASSERT(self->name != THORIUM_ACTOR_NOBODY);
}

void thorium_actor_destroy(struct thorium_actor *self)
{
    thorium_actor_init_fn_t destroy;
    struct thorium_message message;

    /* The concrete actor must first be destroyed.
     */
    destroy = thorium_actor_get_destroy(self);
    destroy(self);

    /*
     * Make sure that everyone see that this actor is
     * dead.
     */
    bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_DEAD);

    bsal_memory_fence();

    thorium_dispatcher_destroy(&self->dispatcher);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_vector_destroy(&self->acquaintance_vector);
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_vector_destroy(&self->children);
#endif

    bsal_queue_destroy(&self->queued_messages_for_clone);
    bsal_queue_destroy(&self->queued_messages_for_migration);
    bsal_queue_destroy(&self->forwarding_queue);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    bsal_map_destroy(&self->acquaintance_map);
#endif

    bsal_map_destroy(&self->received_messages);
    bsal_map_destroy(&self->sent_messages);

    BSAL_DEBUGGER_ASSERT(self->worker != NULL);

    while (bsal_queue_dequeue(&self->enqueued_messages, &message)) {

        thorium_worker_free_message(self->worker, &message);
    }

    bsal_queue_destroy(&self->enqueued_messages);

    BSAL_DEBUGGER_ASSERT(bsal_fast_ring_empty(&self->mailbox));

    self->name = -1;

    self->script = NULL;
    self->worker = NULL;
    self->concrete_actor = NULL;

    /* unlock the actor if the actor is being destroyed while
     * being locked
     */
    thorium_actor_unlock(self);

    bsal_lock_destroy(&self->receive_lock);

    /* when exiting the destructor, the actor is unlocked
     * and destroyed too
     */

    bsal_fast_ring_destroy(&self->mailbox);
}

int thorium_actor_name(struct thorium_actor *self)
{
    BSAL_DEBUGGER_ASSERT(self != NULL);

    return self->name;
}

thorium_actor_receive_fn_t thorium_actor_get_receive(struct thorium_actor *self)
{
    return thorium_script_get_receive(self->script);
}

void thorium_actor_set_name(struct thorium_actor *self, int name)
{
    self->name = name;
}

void thorium_actor_print(struct thorium_actor *self)
{
    /* with -Werror -Wall:
     * engine/thorium_actor.c:58:21: error: ISO C for bids conversion of function pointer to object pointer type [-Werror=edantic]
     */

    int received = (int)bsal_counter_get(&self->counter, BSAL_COUNTER_RECEIVED_MESSAGES);
    int sent = (int)bsal_counter_get(&self->counter, BSAL_COUNTER_SENT_MESSAGES);

    printf("INSPECT actor: %s/%d\n",
                        thorium_actor_script_name(self),
                        thorium_actor_name(self));

    printf("[thorium_actor_print] Name: %i Supervisor %i Node: %i, Thread: %i"
                    " received %i sent %i\n", thorium_actor_name(self),
                    thorium_actor_supervisor(self),
                    thorium_node_name(thorium_actor_node(self)),
                    thorium_worker_name(thorium_actor_worker(self)),
                    received, sent);
}

thorium_actor_init_fn_t thorium_actor_get_init(struct thorium_actor *self)
{
    return thorium_script_get_init(self->script);
}

thorium_actor_destroy_fn_t thorium_actor_get_destroy(struct thorium_actor *self)
{
    return thorium_script_get_destroy(self->script);
}

void thorium_actor_set_worker(struct thorium_actor *self, struct thorium_worker *worker)
{
    self->worker = worker;
}

int thorium_actor_send_system_self(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;

    tag = thorium_message_tag(message);

#if 0
    if (tag == ACTION_PIN_TO_WORKER) {
        thorium_actor_pin_to_worker(self);
        return 1;

    } else if (tag == ACTION_UNPIN_FROM_WORKER) {
        thorium_actor_unpin_from_worker(self);
        return 1;

    if (tag == ACTION_PIN_TO_NODE) {
        thorium_actor_pin_to_node(self);
        return 1;

    } else if (tag == ACTION_UNPIN_FROM_NODE) {
        thorium_actor_unpin_from_node(self);
        return 1;

#endif

    if (tag == ACTION_PACK_ENABLE) {

            /*
        printf("DEBUG actor %d enabling can_pack\n",
                        thorium_actor_name(self));
                        */

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_CAN_PACK);

        /*
        thorium_actor_send_to_self_empty(self, ACTION_UNPIN_FROM_WORKER);
        thorium_actor_send_to_self_empty(self, ACTION_UNPIN_FROM_NODE);
        */

        return 1;

    } else if (tag == ACTION_PACK_DISABLE) {
        bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_CAN_PACK);
        return 1;

    } else if (tag == ACTION_YIELD) {
        thorium_actor_send_to_self_empty(self, ACTION_YIELD_REPLY);
        return 1;

    } else if (tag == ACTION_STOP) {

        thorium_actor_die(self);
        return 1;
    }

    return 0;
}

int thorium_actor_send_system(struct thorium_actor *self, int name, struct thorium_message *message)
{
    int self_name;

    self_name = thorium_actor_name(self);

    /* Verify if the message is a special message.
     * For instance, it is important to pin an
     * actor right away if it is requested.
     */
    if (name == self_name) {
        if (thorium_actor_send_system_self(self, message)) {
            return 1;
        }
    }

    return 0;
}

void thorium_actor_send(struct thorium_actor *self, int name, struct thorium_message *message)
{
    int source;
    int *bucket;

    /* Update counter
     */
    bucket = (int *)bsal_map_get(&self->sent_messages, &name);

    if (bucket == NULL) {
        bucket = (int *)bsal_map_add(&self->sent_messages, &name);
        (*bucket) = 0;
    }

    (*bucket)++;

    source = thorium_actor_name(self);

    /* update counters
     */
    if (source == name) {
        bsal_counter_add(&self->counter, BSAL_COUNTER_SENT_MESSAGES_TO_SELF, 1);
        bsal_counter_add(&self->counter, BSAL_COUNTER_SENT_BYTES_TO_SELF,
                        thorium_message_count(message));
    } else {
        bsal_counter_add(&self->counter, BSAL_COUNTER_SENT_MESSAGES_NOT_TO_SELF, 1);
        bsal_counter_add(&self->counter, BSAL_COUNTER_SENT_BYTES_NOT_TO_SELF,
                        thorium_message_count(message));
    }

    if (thorium_actor_send_system(self, name, message)) {
        return;
    }

    thorium_actor_send_with_source(self, name, message, source);
}

void thorium_actor_send_with_source(struct thorium_actor *self, int name, struct thorium_message *message,
                int source)
{
    int tag;

    tag = thorium_message_tag(message);
    thorium_message_set_source(message, source);
    thorium_message_set_destination(message, name);

#ifdef THORIUM_ACTOR_DEBUG9
    if (thorium_message_tag(message) == 1100) {
        printf("DEBUG thorium_message_set_source 1100\n");
    }
#endif

    /* messages sent in the init or destroy are not sent
     * at all !
     */
    if (self->worker == NULL) {

        printf("Error, message was lost because it was sent in *_init() or *_destroy(), which is not allowed (tag: %d)\n",
                        tag);

        return;
    }

    thorium_worker_send(self->worker, message);
}

int thorium_actor_spawn(struct thorium_actor *self, int script)
{
    int name;

    name = thorium_actor_spawn_real(self, script);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    thorium_actor_add_child(self, name);
#endif

#ifdef THORIUM_ACTOR_DEBUG_SPAWN
    printf("acquaintances after spawning\n");
    bsal_vector_print_int(&self->acquaintance_vector);
    printf("\n");
#endif

    return name;
}

int thorium_actor_spawn_real(struct thorium_actor *self, int script)
{
    int name;
    int self_name = thorium_actor_name(self);

#ifdef THORIUM_ACTOR_DEBUG_SPAWN
    printf("DEBUG thorium_actor_spawn script %d\n", script);
#endif

    name = thorium_node_spawn(thorium_actor_node(self), script);

    if (name == THORIUM_ACTOR_NOBODY) {
        printf("Error: problem with spawning! did you register the script ?\n");
        return name;
    }

#ifdef THORIUM_ACTOR_DEBUG_SPAWN
    printf("DEBUG thorium_actor_spawn before set_supervisor, spawned %d\n",
                    name);
#endif

    thorium_node_set_supervisor(thorium_actor_node(self), name, self_name);

    bsal_counter_add(&self->counter, BSAL_COUNTER_SPAWNED_ACTORS, 1);

    return name;
}

void thorium_actor_die(struct thorium_actor *self)
{
    bsal_counter_add(&self->counter, BSAL_COUNTER_KILLED_ACTORS, 1);
    bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_DEAD);

    /*
     * Publish the memory transaction so that other threads see it
     * too.
     */
    bsal_memory_fence();
}

struct bsal_counter *thorium_actor_counter(struct thorium_actor *self)
{
    return &self->counter;
}

struct thorium_node *thorium_actor_node(struct thorium_actor *self)
{
    if (self->node != NULL) {
        return self->node;
    }

    if (self->worker == NULL) {
        return NULL;
    }

    return thorium_worker_node(thorium_actor_worker(self));
}

void thorium_actor_lock(struct thorium_actor *self)
{
    bsal_lock_lock(&self->receive_lock);
    bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_LOCKED);
}

void thorium_actor_unlock(struct thorium_actor *self)
{
    if (!bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_LOCKED)) {
        return;
    }

    bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_LOCKED);
    bsal_lock_unlock(&self->receive_lock);
}

int thorium_actor_argc(struct thorium_actor *self)
{
    return thorium_node_argc(thorium_actor_node(self));
}

char **thorium_actor_argv(struct thorium_actor *self)
{
    return thorium_node_argv(thorium_actor_node(self));
}

int thorium_actor_supervisor(struct thorium_actor *self)
{
    return self->supervisor;
}

void thorium_actor_set_supervisor(struct thorium_actor *self, int supervisor)
{
    self->supervisor = supervisor;
}

int thorium_actor_receive_system_no_pack(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;

    tag = thorium_message_tag(message);

    if (tag == ACTION_PACK) {

        thorium_actor_send_reply_empty(self, ACTION_PACK_REPLY);
        return 1;

    } else if (tag == ACTION_PACK_SIZE) {
        thorium_actor_send_reply_int(self, ACTION_PACK_SIZE_REPLY, 0);
        return 1;

    } else if (tag == ACTION_UNPACK) {
        thorium_actor_send_reply_empty(self, ACTION_PACK_REPLY);
        return 1;

    } else if (tag == ACTION_CLONE) {

        /* return nothing if the cloning is not supported or
         * if a cloning is already in progress, the message will be queued below.
         */

            /*
        printf("DEBUG actor %d ACTION_CLONE not supported can_pack %d\n", thorium_actor_name(self),
                        */

        thorium_actor_send_reply_int(self, ACTION_CLONE_REPLY, THORIUM_ACTOR_NOBODY);
        return 1;

    } else if (tag == ACTION_MIGRATE) {

        /* return nothing if the cloning is not supported or
         * if a cloning is already in progress
         */

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate: pack not supported\n");
#endif

        thorium_actor_send_reply_int(self, ACTION_MIGRATE_REPLY, THORIUM_ACTOR_NOBODY);

        return 1;
    }

    return 0;
}

int thorium_actor_receive_system(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;
    int name;
    int source;
    int spawned;
    int script;
    void *buffer;
    int count;
    int old_supervisor;
    int supervisor;
    int new_count;
    void *new_buffer;
    struct thorium_message new_message;
    int offset;
    int bytes;
    struct bsal_memory_pool *ephemeral_memory;
    int workers;

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    int new_actor;
#endif

    ephemeral_memory = thorium_actor_get_ephemeral_memory(self);
    tag = thorium_message_tag(message);

    /* the concrete actor must catch these otherwise.
     * Also, clone and migrate depend on these.
     */
    if (!bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_CAN_PACK)) {

        if (thorium_actor_receive_system_no_pack(self, message)) {
            return 1;
        }
    }

    name = thorium_actor_name(self);
    source =thorium_message_source(message);
    buffer = thorium_message_buffer(message);
    count = thorium_message_count(message);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    /* For any remote spawning operation, add the new actor in the list of
     * children
     */
    if (tag == ACTION_SPAWN_REPLY) {

        new_actor = *(int *)buffer;
        thorium_actor_add_child(self, new_actor);
    }
#endif

    /* check message tags that are required for migration
     * before attempting to queue messages during hot actor
     * migration
     */

    /* cloning workflow in 4 easy steps !
     */
    if (tag == ACTION_CLONE) {

        if (self->cloning_status == THORIUM_ACTOR_STATUS_NOT_STARTED) {

            /* begin the cloning operation */
            thorium_actor_clone(self, message);

        } else {
            /* queue the cloning message */
#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
            printf("DEBUG thorium_actor_receive_system queuing message %d because cloning is in progress\n",
                            tag);
#endif

            self->forwarding_selector = THORIUM_ACTOR_FORWARDING_CLONE;
            thorium_actor_queue_message(self, message);
        }

        return 1;

    } else if (self->cloning_status == THORIUM_ACTOR_STATUS_STARTED) {

        /* call a function called
         * thorium_actor_continue_clone
         */
        bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_CLONING_PROGRESSED);
        thorium_actor_continue_clone(self, message);

        if (bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_CLONING_PROGRESSED)) {
            return 1;
        }
    }

    if (self->migration_status == THORIUM_ACTOR_STATUS_STARTED) {

        bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);
        thorium_actor_migrate(self, message);

        if (bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED)) {
            return 1;
        }
    }

    /* spawn an actor.
     * This works even during migration because the supervisor is the
     * source of ACTION_SPAWN...
     */
    if (tag == ACTION_SPAWN) {
        script = *(int *)buffer;
        spawned = thorium_actor_spawn_real(self, script);

#ifdef THORIUM_ACTOR_DEBUG_SPAWN
        printf("DEBUG setting supervisor of %d to %d\n", spawned, source);
#endif

        thorium_node_set_supervisor(thorium_actor_node(self), spawned, source);

        new_buffer = bsal_memory_pool_allocate(ephemeral_memory, 2 * sizeof(int));
        offset = 0;

        bytes = sizeof(spawned);
        memcpy((char *)new_buffer + offset, &spawned, bytes);
        offset += bytes;
        bytes = sizeof(script);
        memcpy((char *)new_buffer + offset, &script, bytes);
        offset += bytes;

        new_count = offset;
        thorium_message_init(&new_message, ACTION_SPAWN_REPLY, new_count, new_buffer);
        thorium_actor_send(self, source, &new_message);

        thorium_message_destroy(&new_message);
        bsal_memory_pool_free(ephemeral_memory, new_buffer);

        return 1;

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    } else if (tag == ACTION_MIGRATE_NOTIFY_ACQUAINTANCES) {
        thorium_actor_migrate_notify_acquaintances(self, message);
        return 1;

    } else if (tag == ACTION_NOTIFY_NAME_CHANGE) {

        thorium_actor_notify_name_change(self, message);
        return 1;
#endif

    } else if (tag == ACTION_NOTIFY_NAME_CHANGE_REPLY) {

        thorium_actor_send_to_self_empty(self, ACTION_MIGRATE_NOTIFY_ACQUAINTANCES);

        return 1;

    } else if (tag == ACTION_PACK && source == name) {
        /* ACTION_PACK has to go through during live migration
         */
        return 0;

    } else if (tag == ACTION_PROXY_MESSAGE) {
        thorium_actor_receive_proxy_message(self, message);
        return 1;

    } else if (tag == ACTION_FORWARD_MESSAGES_REPLY) {
        /* This message is a system message, it is not for the concrete
         * actor
         */
        return 1;
    }

    /* at this point, the remaining possibilities for the message tag
     * of the current message are all not required to perform
     * actor migration
     */

    /* queue messages during a hot migration
     *
     * ACTION_CLONE messsages are also queued during cloning...
     */
    if (self->migration_status == THORIUM_ACTOR_STATUS_STARTED) {

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_receive_system queuing message %d during migration\n",
                        tag);
#endif
        self->forwarding_selector = THORIUM_ACTOR_FORWARDING_MIGRATE;
        thorium_actor_queue_message(self, message);
        return 1;
    }


    /* Perform binomial routing.
     */
    if (tag == ACTION_BINOMIAL_TREE_SEND) {
        thorium_actor_receive_binomial_tree_send(self, message);
        return 1;

    } else if (tag == ACTION_MIGRATE) {

        thorium_actor_migrate(self, message);
        return 1;

    } else if (tag == ACTION_SYNCHRONIZE) {
        /* the concrete actor must catch this one */

    } else if (tag == ACTION_SYNCHRONIZE_REPLY) {
        thorium_actor_receive_synchronize_reply(self, message);

        /* we also allow the concrete actor to receive this */

    /* Ignore THORIUM_ACTOR_PIN and THORIUM_ACTOR_UNPIN
     * because they can only be sent by an actor
     * to itself.
     */

        /*
    } else if (tag == ACTION_PIN_TO_WORKER) {
        return 1;

    } else if (tag == ACTION_UNPIN_FROM_WORKER) {
        return 1;

    } else if (tag == ACTION_PIN_TO_NODE) {
        return 1;

    } else if (tag == ACTION_UNPIN_FROM_NODE) {
        return 1;
*/
    } else if (tag == ACTION_SET_SUPERVISOR
                    /*&& source == thorium_actor_supervisor(self)*/) {

    /* only an actor that knows the name of
     * the current supervisor can assign a new supervisor
     * this information can not be obtained by default
     * for security reasons.
     */

        if (count != 2 * sizeof(int)) {
            return 1;
        }

        thorium_message_unpack_int(message, 0, &old_supervisor);
        thorium_message_unpack_int(message, sizeof(old_supervisor), &supervisor);

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_receive_system actor %d receives ACTION_SET_SUPERVISOR old supervisor %d (provided %d), new supervisor %d\n",
                        thorium_actor_name(self),
                        thorium_actor_supervisor(self), old_supervisor,
                        supervisor);
#endif

        if (thorium_actor_supervisor(self) == old_supervisor) {

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
            printf("DEBUG thorium_actor_receive_system authentification successful\n");
#endif
            thorium_actor_set_supervisor(self, supervisor);
        }

        thorium_actor_send_reply_empty(self, ACTION_SET_SUPERVISOR_REPLY);

        return 1;

    /* ACTION_SYNCHRONIZE can not be queued.
     */
    } else if (tag == ACTION_SYNCHRONIZE) {
        return 1;

    /* ACTION_SYNCHRONIZED can only be sent to an actor
     * by itself.
     */
    } else if (tag == ACTION_SYNCHRONIZED && name != source) {
        return 1;

    /* block ACTION_STOP if it is not from self
     * acquaintance actors have to use ACTION_ASK_TO_STOP
     * in fact, this message has to be sent by the actor
     * itself.
     */
    } else if (tag == ACTION_STOP) {

        return 1;

    } else if (tag == ACTION_GET_NODE_NAME) {

        thorium_actor_send_reply_int(self, ACTION_GET_NODE_NAME_REPLY,
                        thorium_actor_node_name(self));
        return 1;

    } else if (tag == ACTION_GET_NODE_WORKER_COUNT) {

        workers = thorium_actor_node_worker_count(self);

        printf("DEBUG actor %d ACTION_GET_NODE_WORKER_COUNT %d workers\n",
                        thorium_actor_name(self),
                        workers);

        thorium_actor_send_reply_int(self, ACTION_GET_NODE_WORKER_COUNT_REPLY,
                        workers);
        return 1;

    } else  if (tag == ACTION_FORWARD_MESSAGES) {

        thorium_actor_forward_messages(self, message);
        return 1;

    }

    return 0;
}

void thorium_actor_receive(struct thorium_actor *self, struct thorium_message *message)
{
    thorium_actor_receive_fn_t receive;
    int name;
    int source;
    int *bucket;

#ifdef THORIUM_ACTOR_DEBUG_SYNC
    printf("\nDEBUG thorium_actor_receive...... tag %d\n",
                    thorium_message_tag(message));

    if (thorium_message_tag(message) == ACTION_SYNCHRONIZED) {
        printf("DEBUG =============\n");
        printf("DEBUG thorium_actor_receive before concrete receive ACTION_SYNCHRONIZED\n");
    }

    printf("DEBUG thorium_actor_receive tag %d for %d\n",
                    thorium_message_tag(message),
                    thorium_actor_name(self));
#endif

    /* Update counter
     */
    source = thorium_message_source(message);

    self->current_source = source;
    bucket = (int *)bsal_map_get(&self->received_messages, &source);

    if (bucket == NULL) {
        bucket = (int *)bsal_map_add(&self->received_messages, &source);
        (*bucket) = 0;
    }

    (*bucket)++;

    /* check if this is a message that the system can
     * figure out what to do with it
     */
    if (thorium_actor_receive_system(self, message)) {
        return;

#ifdef THORIUM_ACTOR_DO_DISPATCH_IN_ABSTRACT_ACTOR
    /* otherwise, verify if the actor registered a
     * handler for this tag
     */
    } else if (thorium_actor_take_action(self, message)) {
        return;
#endif
    }


    /* Otherwise, this is a message for the actor itself.
     */
    receive = thorium_actor_get_receive(self);

    BSAL_DEBUGGER_ASSERT(receive != NULL);

#ifdef THORIUM_ACTOR_DEBUG_SYNC
    printf("DEBUG thorium_actor_receive calls concrete receive tag %d\n",
                    thorium_message_tag(message));
#endif

    name = thorium_actor_name(self);

    /* update counters
     */
    if (source == name) {
        bsal_counter_add(&self->counter, BSAL_COUNTER_RECEIVED_MESSAGES_FROM_SELF, 1);
        bsal_counter_add(&self->counter, BSAL_COUNTER_RECEIVED_BYTES_FROM_SELF,
                        thorium_message_count(message));
    } else {
        bsal_counter_add(&self->counter, BSAL_COUNTER_RECEIVED_MESSAGES_NOT_FROM_SELF, 1);
        bsal_counter_add(&self->counter, BSAL_COUNTER_RECEIVED_BYTES_NOT_FROM_SELF,
                        thorium_message_count(message));
    }

    receive(self, message);
}

void thorium_actor_receive_proxy_message(struct thorium_actor *self,
                struct thorium_message *message)
{
    int source;

    source = thorium_actor_unpack_proxy_message(self, message);
    thorium_actor_send_with_source(self, thorium_actor_name(self),
                    message, source);
}

void thorium_actor_receive_synchronize(struct thorium_actor *self,
                struct thorium_message *message)
{

#ifdef THORIUM_ACTOR_DEBUG
    printf("DEBUG56 replying to %i with THORIUM_ACTOR_PRIVATE_SYNCHRONIZE_REPLY\n",
                    thorium_message_source(message));
#endif

    thorium_message_init(message, ACTION_SYNCHRONIZE_REPLY, 0, NULL);
    thorium_actor_send(self, thorium_message_source(message), message);

    thorium_message_destroy(message);
}

void thorium_actor_receive_synchronize_reply(struct thorium_actor *self,
                struct thorium_message *message)
{
    int name;

    if (bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_SYNCHRONIZATION_STARTED)) {

#ifdef THORIUM_ACTOR_DEBUG
        printf("DEBUG99 synchronization_reply %i/%i\n",
                        self->synchronization_responses,
                        self->synchronization_expected_responses);
#endif

        self->synchronization_responses++;

        /* send ACTION_SYNCHRONIZED to self
         */
        if (thorium_actor_synchronization_completed(self)) {

#ifdef THORIUM_ACTOR_DEBUG_SYNC
            printf("DEBUG sending ACTION_SYNCHRONIZED to self\n");
#endif
            struct thorium_message new_message;
            thorium_message_init(&new_message, ACTION_SYNCHRONIZED,
                            sizeof(self->synchronization_responses),
                            &self->synchronization_responses);

            name = thorium_actor_name(self);

            thorium_actor_send(self, name, &new_message);
            bsal_bitmap_clear_bit_uint32_t(&self->flags, FLAG_SYNCHRONIZATION_STARTED);
        }
    }
}

void thorium_actor_synchronize(struct thorium_actor *self, struct bsal_vector *actors)
{
    struct thorium_message message;

    bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_SYNCHRONIZATION_STARTED);
    self->synchronization_expected_responses = bsal_vector_size(actors);
    self->synchronization_responses = 0;

    /* emit synchronization
     */

#ifdef THORIUM_ACTOR_DEBUG
    printf("DEBUG actor %i emit synchronization %i-%i, expected: %i\n",
                    thorium_actor_name(self), first, last,
                    self->synchronization_expected_responses);
#endif

    thorium_message_init(&message, ACTION_SYNCHRONIZE, 0, NULL);

    /* TODO thorium_actor_send_range_binomial_tree is broken */
    thorium_actor_send_range(self, actors, &message);
    thorium_message_destroy(&message);
}

int thorium_actor_synchronization_completed(struct thorium_actor *self)
{
    if (bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_SYNCHRONIZATION_STARTED) == 0) {
        return 0;
    }

#ifdef THORIUM_ACTOR_DEBUG
    printf("DEBUG32 actor %i thorium_actor_synchronization_completed %i/%i\n",
                    thorium_actor_name(self),
                    self->synchronization_responses,
                    self->synchronization_expected_responses);
#endif

    if (self->synchronization_responses == self->synchronization_expected_responses) {
        return 1;
    }

    return 0;
}

int thorium_actor_unpack_proxy_message(struct thorium_actor *self,
                struct thorium_message *message)
{
    int new_count;
    int tag;
    int source;
    void *buffer;
    int offset;

    buffer = thorium_message_buffer(message);
    new_count = thorium_message_count(message);
    new_count -= sizeof(source);
    new_count -= sizeof(tag);

    offset = new_count;

    source = *(int *)((char *)buffer + offset);
    offset += sizeof(source);
    tag = *(int *)((char *)buffer + offset);
    offset += sizeof(tag);

    /* TODO, verify if it is new_count or old count
     */
    thorium_message_init(message, tag, new_count, buffer);

    return source;
}

void thorium_actor_pack_proxy_message(struct thorium_actor *self, struct thorium_message *message,
                int real_source)
{
    int real_tag;
    int count;
    int new_count;
    void *buffer;
    void *new_buffer;
    int offset;

    real_tag = thorium_message_tag(message);
    buffer = thorium_message_buffer(message);
    count = thorium_message_count(message);

    new_count = count + sizeof(real_source) + sizeof(real_tag);

    /* TODO: use slab allocator */
    new_buffer = bsal_memory_allocate(new_count);

#ifdef THORIUM_ACTOR_DEBUG
    printf("DEBUG12 bsal_memory_pool_allocate %p (pack proxy message)\n",
                    new_buffer);
#endif

    memcpy(new_buffer, buffer, count);

    offset = count;
    memcpy((char *)new_buffer + offset, &real_source, sizeof(real_source));
    offset += sizeof(real_source);
    memcpy((char *)new_buffer + offset, &real_tag, sizeof(real_tag));
    offset += sizeof(real_tag);

    thorium_message_init(message, ACTION_PROXY_MESSAGE, new_count, new_buffer);
    thorium_message_set_source(message, real_source);

    /* free the old buffer
     */
    bsal_memory_free(buffer);
    buffer = NULL;
}

int thorium_actor_script(struct thorium_actor *self)
{
    BSAL_DEBUGGER_ASSERT(self != NULL);

    BSAL_DEBUGGER_ASSERT(self->script != NULL);

    return thorium_script_identifier(self->script);
}

void thorium_actor_add_script(struct thorium_actor *self, int name, struct thorium_script *script)
{
    thorium_node_add_script(thorium_actor_node(self), name, script);
}

void thorium_actor_clone(struct thorium_actor *self, struct thorium_message *message)
{
    int spawner;
    void *buffer;
    int script;
    struct thorium_message new_message;
    int source;

    script = thorium_actor_script(self);
    source = thorium_message_source(message);
    buffer = thorium_message_buffer(message);
    spawner = *(int *)buffer;
    self->cloning_spawner = spawner;
    self->cloning_client = source;

#ifdef THORIUM_ACTOR_DEBUG_CLONE
    int name;
    name = thorium_actor_name(self);
    printf("DEBUG %d sending ACTION_SPAWN to spawner %d for client %d\n", name, spawner,
                    source);
#endif

    thorium_message_init(&new_message, ACTION_SPAWN, sizeof(script), &script);
    thorium_actor_send(self, spawner, &new_message);

    self->cloning_status = THORIUM_ACTOR_STATUS_STARTED;
}

void thorium_actor_continue_clone(struct thorium_actor *self, struct thorium_message *message)
{
    int tag;
    int source;
    int self_name;
    struct thorium_message new_message;
    int count;
    void *buffer;

    count = thorium_message_count(message);
    buffer = thorium_message_buffer(message);
    self_name = thorium_actor_name(self);
    tag = thorium_message_tag(message);
    source = thorium_message_source(message);

#ifdef THORIUM_ACTOR_DEBUG_CLONE1
    printf("DEBUG thorium_actor_continue_clone source %d tag %d\n", source, tag);
#endif

    if (tag == ACTION_SPAWN_REPLY && source == self->cloning_spawner) {

        self->cloning_new_actor = *(int *)buffer;

#ifdef THORIUM_ACTOR_DEBUG_CLONE
        printf("DEBUG thorium_actor_continue_clone ACTION_SPAWN_REPLY NEW ACTOR IS %d\n",
                        self->cloning_new_actor);
#endif

        thorium_actor_send_to_self_empty(self, ACTION_PACK);

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_CLONING_PROGRESSED);

    } else if (tag == ACTION_PACK_REPLY && source == self_name) {

#ifdef THORIUM_ACTOR_DEBUG_CLONE
        printf("DEBUG thorium_actor_continue_clone ACTION_PACK_REPLY sending UNPACK to %d\n",
                         self->cloning_new_actor);
#endif

        /* forward the buffer to the new actor */
        thorium_message_init(&new_message, ACTION_UNPACK, count, buffer);
        thorium_actor_send(self, self->cloning_new_actor, &new_message);

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_CLONING_PROGRESSED);

        thorium_message_destroy(&new_message);

    } else if (tag == ACTION_UNPACK_REPLY && source == self->cloning_new_actor) {

            /*
    } else if (tag == ACTION_FORWARD_MESSAGES_REPLY) {
#ifdef THORIUM_ACTOR_DEBUG_CLONE
        printf("DEBUG thorium_actor_continue_clone ACTION_UNPACK_REPLY\n");
#endif
*/
        /* it is required that the cloning process be concluded at this point because otherwise
         * queued messages will be queued when they are being forwarded.
         */
        thorium_message_init(&new_message, ACTION_CLONE_REPLY, sizeof(self->cloning_new_actor),
                        &self->cloning_new_actor);
        thorium_actor_send(self, self->cloning_client, &new_message);

        /* we are ready for another cloning */
        self->cloning_status = THORIUM_ACTOR_STATUS_NOT_STARTED;

#ifdef THORIUM_ACTOR_DEBUG_CLONE
        printf("actor:%d sends clone %d to client %d\n", thorium_actor_name(self),
                        self->cloning_new_actor, self->cloning_client);
#endif

        self->forwarding_selector = THORIUM_ACTOR_FORWARDING_CLONE;

#ifdef THORIUM_ACTOR_DEBUG_CLONE
        printf("DEBUG clone finished, forwarding queued messages (if any) to %d, queue/%d\n",
                        thorium_actor_name(self), self->forwarding_selector);
#endif

        thorium_actor_send_to_self_empty(self, ACTION_FORWARD_MESSAGES);

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_CLONING_PROGRESSED);
    }
}

int thorium_actor_source(struct thorium_actor *self)
{
    return self->current_source;
}

int thorium_actor_node_name(struct thorium_actor *self)
{
    return thorium_node_name(thorium_actor_node(self));
}

int thorium_actor_get_node_count(struct thorium_actor *self)
{
    return thorium_node_nodes(thorium_actor_node(self));
}

int thorium_actor_node_worker_count(struct thorium_actor *self)
{
    return thorium_node_worker_count(thorium_actor_node(self));
}

int thorium_actor_take_action(struct thorium_actor *self, struct thorium_message *message)
{

#ifdef THORIUM_ACTOR_DEBUG_10335
    if (thorium_message_tag(message) == 10335) {
        printf("DEBUG actor %d thorium_actor_dispatch 10335\n",
                        thorium_actor_name(self));
    }
#endif

    return thorium_dispatcher_dispatch(&self->dispatcher, self, message);
}

struct thorium_dispatcher *thorium_actor_dispatcher(struct thorium_actor *self)
{
    return &self->dispatcher;
}

void thorium_actor_set_node(struct thorium_actor *self, struct thorium_node *node)
{
    self->node = node;
}

void thorium_actor_migrate(struct thorium_actor *self, struct thorium_message *message)
{
    int spawner;
    void *buffer;
    int source;
    int tag;
    int name;
    struct thorium_message new_message;
    int data[2];
    int selector;

    tag = thorium_message_tag(message);
    source = thorium_message_source(message);
    name = thorium_actor_name(self);

    /*
     * For migration, the same name is kept
     */

    thorium_actor_send_reply_int(self, ACTION_MIGRATE_REPLY,
                    thorium_actor_name(self));

    return;

    if (bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_MIGRATION_CLONED) == 0) {

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate thorium_actor_migrate: cloning self...\n");
#endif

        /* clone self
         */
        source = thorium_message_source(message);
        buffer = thorium_message_buffer(message);
        spawner = *(int *)buffer;
        name = thorium_actor_name(self);

        self->migration_spawner = spawner;
        self->migration_client = source;

        thorium_actor_send_to_self_int(self, ACTION_CLONE, spawner);

        self->migration_status = THORIUM_ACTOR_STATUS_STARTED;
        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_CLONED);

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);

    } else if (tag == ACTION_CLONE_REPLY && source == name) {

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate thorium_actor_migrate: cloned.\n");
#endif

        /* tell acquaintances that the clone is the new original.
         */
        thorium_message_unpack_int(message, 0, &self->migration_new_actor);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
        self->acquaintance_index = 0;
#endif
        thorium_actor_send_to_self_empty(self, ACTION_MIGRATE_NOTIFY_ACQUAINTANCES);

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate: notify acquaintance of name change.\n");
#endif
        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);

    } else if (tag == ACTION_MIGRATE_NOTIFY_ACQUAINTANCES_REPLY && source == name) {

        /* at this point, there should not be any new messages addressed
         * to the old name if all the code implied uses
         * acquaintance vectors.
         */
        /* assign the supervisor of the original version
         * of the migrated actor to the new version
         * of the migrated actor
         */
#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate actor %d setting supervisor of %d to %d\n",
                        thorium_actor_name(self),
                        self->migration_new_actor,
                        thorium_actor_supervisor(self));
#endif

        data[0] = thorium_actor_name(self);
        data[1] = thorium_actor_supervisor(self);

        thorium_message_init(&new_message, ACTION_SET_SUPERVISOR,
                        2 * sizeof(int), data);
        thorium_actor_send(self, self->migration_new_actor, &new_message);
        thorium_message_destroy(&new_message);

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);

    } else if (tag == ACTION_FORWARD_MESSAGES_REPLY
                    && bsal_bitmap_get_bit_uint32_t(&self->flags,
                            FLAG_MIGRATION_FORWARDED_MESSAGES)) {

        /* send the name of the new copy and die of a peaceful death.
         */
        thorium_actor_send_int(self, self->migration_client, ACTION_MIGRATE_REPLY,
                        self->migration_new_actor);

        thorium_actor_send_to_self_empty(self, ACTION_STOP);

        self->migration_status = THORIUM_ACTOR_STATUS_NOT_STARTED;

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
        printf("DEBUG thorium_actor_migrate: OK, now killing self and returning clone name to client.\n");
#endif

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);

    } else if (tag == ACTION_SET_SUPERVISOR_REPLY
                    && source == self->migration_new_actor) {

        if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_NONE) {

            /* the forwarding system is ready to be used.
             */
            self->forwarding_selector = THORIUM_ACTOR_FORWARDING_MIGRATE;

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
            printf("DEBUG thorium_actor_migrate %d forwarding queued messages to actor %d, queue/%d (forwarding system ready.)\n",
                        thorium_actor_name(self),
                        self->migration_new_actor, self->forwarding_selector);
#endif

            thorium_actor_send_to_self_empty(self, ACTION_FORWARD_MESSAGES);
            bsal_bitmap_set_bit_uint32_t(&self->flags,
                                FLAG_MIGRATION_FORWARDED_MESSAGES);

        /* wait for the clone queue to be empty.
         */
        } else {

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
            printf("DEBUG thorium_actor_migrate queuing system is busy (used by queue/%d), queuing selector\n",
                            self->forwarding_selector);
#endif
            /* queue the selector into the forwarding system
             */
            selector = THORIUM_ACTOR_FORWARDING_MIGRATE;
            bsal_queue_enqueue(&self->forwarding_queue, &selector);
        }

        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_MIGRATION_PROGRESSED);
    }
}

#ifdef THORIUM_ACTOR_STORE_CHILDREN
void thorium_actor_notify_name_change(struct thorium_actor *self, struct thorium_message *message)
{
    int old_name;
    int new_name;
    int source;
    int index;
    int *bucket;
    struct thorium_message new_message;
    int enqueued_messages;

    source = thorium_message_source(message);
    old_name = source;
    thorium_message_unpack_int(message, 0, &new_name);

    /* update the acquaintance vector
     */
    index = thorium_actor_get_acquaintance_index_private(self, old_name);

    bucket = bsal_vector_at(&self->acquaintance_vector, index);

    /*
     * Change it only if it exists
     */
    if (bucket != NULL) {
        *bucket = new_name;
    }

    /* update userland queued messages
     */
    enqueued_messages = thorium_actor_enqueued_message_count(self);

    while (enqueued_messages--) {

        thorium_actor_dequeue_message(self, &new_message);
        if (thorium_message_source(&new_message) == old_name) {
            thorium_message_set_source(&new_message, new_name);
        }
        thorium_actor_enqueue_message(self, &new_message);
    }

    thorium_actor_send_reply_empty(self, ACTION_NOTIFY_NAME_CHANGE_REPLY);
}
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
void thorium_actor_migrate_notify_acquaintances(struct thorium_actor *self, struct thorium_message *message)
{
    struct bsal_vector *acquaintance_vector;
    int acquaintance;

    acquaintance_vector = thorium_actor_acquaintance_vector_private(self);

    if (self->acquaintance_index < bsal_vector_size(acquaintance_vector)) {

        acquaintance = bsal_vector_at_as_int(acquaintance_vector, self->acquaintance_index);
        thorium_actor_send_int(self, acquaintance, ACTION_NOTIFY_NAME_CHANGE,
                        self->migration_new_actor);
        self->acquaintance_index++;

    } else {

        thorium_actor_send_to_self_empty(self, ACTION_MIGRATE_NOTIFY_ACQUAINTANCES_REPLY);
    }
}
#endif

void thorium_actor_send_to_self_proxy(struct thorium_actor *self,
                struct thorium_message *message, int real_source)
{
    int destination;

    destination = thorium_actor_name(self);
    thorium_actor_send_proxy(self, destination, message, real_source);
}

void thorium_actor_send_proxy(struct thorium_actor *self, int destination,
                struct thorium_message *message, int real_source)
{
    struct thorium_message new_message;

    thorium_message_init_copy(&new_message, message);

    thorium_actor_pack_proxy_message(self, &new_message, real_source);
    thorium_actor_send(self, destination, &new_message);

    thorium_message_destroy(&new_message);
}

void thorium_actor_queue_message(struct thorium_actor *self,
                struct thorium_message *message)
{
    void *buffer;
    void *new_buffer;
    int count;
    struct thorium_message new_message;
    struct bsal_queue *queue;
    int tag;
    int source;

    thorium_message_get_all(message, &tag, &count, &buffer, &source);

    new_buffer = NULL;

#ifdef THORIUM_ACTOR_DEBUG_MIGRATE
    printf("DEBUG thorium_actor_queue_message queuing message tag= %d to queue queue/%d\n",
                        tag, self->forwarding_selector);
#endif

    if (count > 0) {
        new_buffer = bsal_memory_allocate(count);
        memcpy(new_buffer, buffer, count);
    }

    thorium_message_init(&new_message, tag, count, new_buffer);
    thorium_message_set_source(&new_message,
                    thorium_message_source(message));
    thorium_message_set_destination(&new_message,
                    thorium_message_destination(message));

    queue = NULL;

    if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_CLONE) {
        queue = &self->queued_messages_for_clone;
    } else if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_MIGRATE) {

        queue = &self->queued_messages_for_migration;
    }

    bsal_queue_enqueue(queue, &new_message);
}

void thorium_actor_forward_messages(struct thorium_actor *self, struct thorium_message *message)
{
    struct thorium_message new_message;
    struct bsal_queue *queue;
    int destination;
    void *buffer_to_release;

    queue = NULL;
    destination = -1;

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
    printf("DEBUG thorium_actor_forward_messages using queue/%d\n",
                    self->forwarding_selector);
#endif

    if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_CLONE) {
        queue = &self->queued_messages_for_clone;
        destination = thorium_actor_name(self);

    } else if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_MIGRATE) {

        queue = &self->queued_messages_for_migration;
        destination = self->migration_new_actor;
    }

    if (queue == NULL) {

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
        printf("DEBUG thorium_actor_forward_messages error could not select queue\n");
#endif
        return;
    }

    if (bsal_queue_dequeue(queue, &new_message)) {

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
        printf("DEBUG thorium_actor_forward_messages actor %d forwarding message to actor %d tag is %d,"
                            " real source is %d\n",
                            thorium_actor_name(self),
                            destination,
                            thorium_message_tag(&new_message),
                            thorium_message_source(&new_message));
#endif

        thorium_actor_pack_proxy_message(self, &new_message,
                        thorium_message_source(&new_message));
        thorium_actor_send(self, destination, &new_message);

        buffer_to_release = thorium_message_buffer(&new_message);
        bsal_memory_free(buffer_to_release);

        /* recursive actor call
         */
        thorium_actor_send_to_self_empty(self, ACTION_FORWARD_MESSAGES);
    } else {

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
        printf("DEBUG thorium_actor_forward_messages actor %d has no more messages to forward in queue/%d\n",
                        thorium_actor_name(self), self->forwarding_selector);
#endif

        if (bsal_queue_dequeue(&self->forwarding_queue, &self->forwarding_selector)) {

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
            printf("DEBUG thorium_actor_forward_messages will now using queue (FIFO pop)/%d\n",
                            self->forwarding_selector);
#endif
            if (self->forwarding_selector == THORIUM_ACTOR_FORWARDING_MIGRATE) {
                bsal_bitmap_set_bit_uint32_t(&self->flags,
                                FLAG_MIGRATION_FORWARDED_MESSAGES);
            }

            /* do a recursive call
             */
            thorium_actor_send_to_self_empty(self, ACTION_FORWARD_MESSAGES);
        } else {

#ifdef THORIUM_ACTOR_DEBUG_FORWARDING
            printf("DEBUG thorium_actor_forward_messages the forwarding system is now available.\n");
#endif

            self->forwarding_selector = THORIUM_ACTOR_FORWARDING_NONE;
            /* this is finished
             */
            thorium_actor_send_to_self_empty(self, ACTION_FORWARD_MESSAGES_REPLY);

        }
    }
}

void thorium_actor_pin_to_node(struct thorium_actor *self)
{

}

void thorium_actor_unpin_from_node(struct thorium_actor *self)
{

}

#ifdef THORIUM_ACTOR_STORE_CHILDREN
int thorium_actor_acquaintance_count(struct thorium_actor *self)
{
    return bsal_vector_size(&self->acquaintance_vector);
}
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
int thorium_actor_get_child(struct thorium_actor *self, int index)
{
    int index2;

    if (index < bsal_vector_size(&self->children)) {
        index2 = *(int *)bsal_vector_at(&self->children, index);
        return thorium_actor_get_acquaintance_private(self, index2);
    }

    return THORIUM_ACTOR_NOBODY;
}

int thorium_actor_child_count(struct thorium_actor *self)
{
    return bsal_vector_size(&self->children);
}

int thorium_actor_add_child(struct thorium_actor *self, int name)
{
    int index;

    index = thorium_actor_add_acquaintance_private(self, name);
    bsal_vector_push_back(&self->children, &index);

    return index;
}
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
int thorium_actor_add_acquaintance_private(struct thorium_actor *self, int name)
{
    int index;
    int *bucket;

    index = thorium_actor_get_acquaintance_index_private(self, name);

    if (index >= 0) {
        return index;
    }

    if (name == THORIUM_ACTOR_NOBODY || name < 0) {
        return -1;
    }

    bsal_vector_push_back_int(thorium_actor_acquaintance_vector_private(self),
                    name);

    index = bsal_vector_size(thorium_actor_acquaintance_vector_private(self)) - 1;

    bucket = bsal_map_add(&self->acquaintance_map, &name);
    *bucket = index;

    return index;
}
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
int thorium_actor_get_acquaintance_index_private(struct thorium_actor *self, int name)
{
    int *bucket;

#if 0
    return bsal_vector_index_of(&self->acquaintance_vector, &name);
#endif

    bucket = bsal_map_get(&self->acquaintance_map, &name);

    if (bucket == NULL) {
        return -1;
    }

    return *bucket;
}
#endif

#ifdef THORIUM_ACTOR_STORE_CHILDREN
int thorium_actor_get_child_index(struct thorium_actor *self, int name)
{
    int i;
    int index;
    int child;

    if (name == THORIUM_ACTOR_NOBODY) {
        return -1;
    }

    for (i = 0; i < thorium_actor_child_count(self); i++) {
        index = *(int *)bsal_vector_at(&self->children, i);

#ifdef THORIUM_ACTOR_DEBUG_CHILDREN
        printf("DEBUG index %d\n", index);
#endif

        child = thorium_actor_get_acquaintance_private(self, index);

        if (child == name) {
            return i;
        }
    }

    return -1;
}
#endif

void thorium_actor_enqueue_message(struct thorium_actor *self, struct thorium_message *message)
{
    void *new_buffer;
    int count;
    void *buffer;
    int source;
    int tag;
    struct thorium_message new_message;
    int destination;

    thorium_message_get_all(message, &tag, &count, &buffer, &source);
    destination = thorium_message_destination(message);

    new_buffer = NULL;

    if (buffer != NULL) {
        new_buffer = bsal_memory_allocate(count);
        memcpy(new_buffer, buffer, count);
    }

    thorium_message_init(&new_message, tag, count, new_buffer);
    thorium_message_set_source(&new_message, source);
    thorium_message_set_destination(&new_message, destination);

    bsal_queue_enqueue(&self->enqueued_messages, &new_message);
    thorium_message_destroy(&new_message);
}

void thorium_actor_dequeue_message(struct thorium_actor *self, struct thorium_message *message)
{
    if (thorium_actor_enqueued_message_count(self) == 0) {
        return;
    }

    bsal_queue_dequeue(&self->enqueued_messages, message);
}

int thorium_actor_enqueued_message_count(struct thorium_actor *self)
{
    return bsal_queue_size(&self->enqueued_messages);
}

struct bsal_map *thorium_actor_get_received_messages(struct thorium_actor *self)
{
    return &self->received_messages;
}

struct bsal_map *thorium_actor_get_sent_messages(struct thorium_actor *self)
{
    return &self->sent_messages;
}

int thorium_actor_enqueue_mailbox_message(struct thorium_actor *self, struct thorium_message *message)
{
    return bsal_fast_ring_push_from_producer(&self->mailbox, message);
}

int thorium_actor_dequeue_mailbox_message(struct thorium_actor *self, struct thorium_message *message)
{
    return bsal_fast_ring_pop_from_consumer(&self->mailbox, message);
}

int thorium_actor_work(struct thorium_actor *self)
{
    struct thorium_message message;
    void *buffer;
    int source_worker;

    if (!thorium_actor_dequeue_mailbox_message(self, &message)) {
        printf("Error, no message...\n");
        thorium_actor_print(self);

        return 0;
    }

    /* Make a copy of the buffer and of the worker
     * because actors can not be trusted.
     */
    buffer = thorium_message_buffer(&message);
    source_worker = thorium_message_get_worker(&message);

    /*
     * Receive the message !
     */
    thorium_actor_receive(self, &message);

    /* Restore the important stuff
     */

    thorium_message_set_buffer(&message, buffer);
    thorium_message_set_worker(&message, source_worker);

    /*
     * Send the buffer back to the source to be recycled.
     */
    thorium_worker_free_message(self->worker, &message);

    /*
     * Check if the actor is dead. If it is, give all the messages
     * to the worker.
     */

    if (thorium_actor_dead(self)) {

        while (thorium_actor_dequeue_mailbox_message(self, &message)) {

            thorium_worker_free_message(self->worker, &message);
        }
    }

    return 1;
}

int thorium_actor_get_mailbox_size(struct thorium_actor *self)
{
    if (thorium_actor_dead(self)) {
        return 0;
    }
    return bsal_fast_ring_size_from_producer(&self->mailbox);
}

int thorium_actor_get_sum_of_received_messages(struct thorium_actor *self)
{
    struct bsal_map_iterator map_iterator;
    struct bsal_map *map;
    int value;
    int messages;

    if (thorium_actor_dead(self)) {
        return 0;
    }
    map = thorium_actor_get_received_messages(self);

    value = 0;

    bsal_map_iterator_init(&map_iterator, map);

    while (bsal_map_iterator_get_next_key_and_value(&map_iterator, NULL, &messages)) {
        value += messages;
    }

    bsal_map_iterator_destroy(&map_iterator);

    return value;
}

char *thorium_actor_script_name(struct thorium_actor *self)
{
    return thorium_script_name(self->script);
}

void thorium_actor_reset_counters(struct thorium_actor *self)
{
#if 0
    struct bsal_map_iterator map_iterator;
    struct bsal_map *map;
    int name;
    int messages;

    map = thorium_actor_get_received_messages(self);

    bsal_map_iterator_init(&map_iterator, map);

    while (bsal_map_iterator_get_next_key_and_value(&map_iterator, &name, &messages)) {
        messages = 0;
        bsal_map_update_value(map, &name, &messages);
    }

    bsal_map_iterator_destroy(&map_iterator);
#endif

    bsal_map_destroy(&self->received_messages);
    bsal_map_init(&self->received_messages, sizeof(int), sizeof(int));
}

int thorium_actor_get_priority(struct thorium_actor *self)
{
    return self->priority;
}

int thorium_actor_get_source_count(struct thorium_actor *self)
{
    return bsal_map_size(&self->received_messages);
}

void thorium_actor_set_priority(struct thorium_actor *self, int priority)
{
    self->priority = priority;
}

void *thorium_actor_concrete_actor(struct thorium_actor *self)
{
    return self->concrete_actor;
}

struct thorium_worker *thorium_actor_worker(struct thorium_actor *self)
{
    return self->worker;
}

int thorium_actor_dead(struct thorium_actor *self)
{
    return bsal_bitmap_get_bit_uint32_t(&self->flags, FLAG_DEAD);
}

/* return 0 if successful
 */
int thorium_actor_trylock(struct thorium_actor *self)
{
    int result;

    result = bsal_lock_trylock(&self->receive_lock);

    if (result == BSAL_LOCK_SUCCESS) {
        bsal_bitmap_set_bit_uint32_t(&self->flags, FLAG_LOCKED);
        return result;
    }

    return result;
}

#ifdef THORIUM_ACTOR_STORE_CHILDREN
struct bsal_vector *thorium_actor_acquaintance_vector_private(struct thorium_actor *self)
{
    return &self->acquaintance_vector;
}

int thorium_actor_get_acquaintance_private(struct thorium_actor *self, int index)
{
    if (index < bsal_vector_size(thorium_actor_acquaintance_vector_private(self))) {
        return bsal_vector_at_as_int(thorium_actor_acquaintance_vector_private(self),
                        index);
    }

    return THORIUM_ACTOR_NOBODY;
}
#endif

struct bsal_memory_pool *thorium_actor_get_ephemeral_memory(struct thorium_actor *self)
{
    struct thorium_worker *worker;

    worker = thorium_actor_worker(self);

    if (worker == NULL) {
        return NULL;
    }

    return thorium_worker_get_ephemeral_memory(worker);
}

int thorium_actor_get_spawner(struct thorium_actor *self, struct bsal_vector *spawners)
{
    int actor;

    if (bsal_vector_size(spawners) == 0) {
        return THORIUM_ACTOR_NOBODY;
    }

    if (self->spawner_index == THORIUM_ACTOR_NO_VALUE) {
        self->spawner_index = bsal_vector_size(spawners) - 1;
    }

    if (self->spawner_index >= bsal_vector_size(spawners)) {
        self->spawner_index = bsal_vector_size(spawners) - 1;
    }

    actor = bsal_vector_at_as_int(spawners, self->spawner_index);

    --self->spawner_index;

    if (self->spawner_index < 0) {

        self->spawner_index = bsal_vector_size(spawners) - 1;
    }

    return actor;
}

struct thorium_script *thorium_actor_get_script(struct thorium_actor *self)
{
    return self->script;
}

void thorium_actor_add_action_with_source_and_condition(struct thorium_actor *self, int tag, thorium_actor_receive_fn_t handler, int source,
        int *actual, int expected)
{
    thorium_dispatcher_add_action(&self->dispatcher, tag, handler, source, actual, expected);
}
