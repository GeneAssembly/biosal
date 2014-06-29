
#include "actor.h"
#include "worker.h"
#include "node.h"

#include <structures/vector_iterator.h>

#include <helpers/actor_helper.h>
#include <helpers/vector_helper.h>
#include <helpers/message_helper.h>

#include <system/memory.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Debugging options
 */

/*
#define BSAL_ACTOR_DEBUG

#define BSAL_ACTOR_DEBUG1

#define BSAL_ACTOR_DEBUG_SYNC
#define BSAL_ACTOR_DEBUG_BINOMIAL_TREE

#define BSAL_ACTOR_DEBUG_SPAWN
#define BSAL_ACTOR_DEBUG_10335
#define BSAL_ACTOR_DEBUG_CLONE

#define BSAL_ACTOR_DEBUG_MIGRATE
#define BSAL_ACTOR_DEBUG_FORWARDING
*/


/* some constants
 */
#define BSAL_ACTOR_ACQUAINTANCE_SUPERVISOR 0

#define BSAL_ACTOR_FORWARDING_NONE 0
#define BSAL_ACTOR_FORWARDING_CLONE 1
#define BSAL_ACTOR_FORWARDING_MIGRATE 2

void bsal_actor_init(struct bsal_actor *actor, void *state,
                struct bsal_script *script, int name, struct bsal_node *node)
{
    bsal_actor_init_fn_t init;

    /* initialize the dispatcher before calling
     * the concrete initializer
     */
    bsal_dispatcher_init(&actor->dispatcher);

    actor->state = state;
    actor->name = name;
    actor->node = node;
    actor->dead = 0;
    actor->script = script;
    actor->worker = NULL;

    actor->synchronization_started = 0;
    actor->synchronization_expected_responses = 0;
    actor->synchronization_responses = 0;

    bsal_lock_init(&actor->receive_lock);
    actor->locked = 0;

    bsal_actor_unpin_from_worker(actor);
    actor->can_pack = BSAL_ACTOR_STATUS_NOT_SUPPORTED;

    actor->cloning_status = BSAL_ACTOR_STATUS_NOT_STARTED;
    actor->migration_status = BSAL_ACTOR_STATUS_NOT_STARTED;
    actor->migration_cloned = 0;
    actor->migration_forwarded_messages = 0;

/*
    printf("DEBUG actor %d init can_pack %d\n",
                    bsal_actor_name(actor), actor->can_pack);
*/
    bsal_vector_init(&actor->acquaintance_vector, sizeof(int));
    bsal_vector_init(&actor->children, sizeof(int));
    bsal_queue_init(&actor->queued_messages_for_clone, sizeof(struct bsal_message));
    bsal_queue_init(&actor->queued_messages_for_migration, sizeof(struct bsal_message));
    bsal_queue_init(&actor->forwarding_queue, sizeof(struct bsal_message));

    bsal_map_init(&actor->acquaintance_map, sizeof(int), sizeof(int));

    bsal_actor_register(actor, BSAL_ACTOR_FORWARD_MESSAGES, bsal_actor_forward_messages);

    bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_UNPIN_FROM_WORKER);
    bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_PIN_TO_NODE);

    bsal_queue_init(&actor->enqueued_messages, sizeof(struct bsal_message));
    bsal_map_init(&actor->received_messages, sizeof(int), sizeof(int));
    bsal_map_init(&actor->sent_messages, sizeof(int), sizeof(int));

    /* call the concrete initializer
     * this must be the last call.
     */
    init = bsal_actor_get_init(actor);
    init(actor);
}

void bsal_actor_destroy(struct bsal_actor *actor)
{
    bsal_actor_init_fn_t destroy;
    struct bsal_message message;
    void *buffer;

    /* The concrete actor must first be destroyed.
     */
    destroy = bsal_actor_get_destroy(actor);
    destroy(actor);

    bsal_dispatcher_destroy(&actor->dispatcher);
    bsal_vector_destroy(&actor->acquaintance_vector);
    bsal_vector_destroy(&actor->children);
    bsal_queue_destroy(&actor->queued_messages_for_clone);
    bsal_queue_destroy(&actor->queued_messages_for_migration);
    bsal_queue_destroy(&actor->forwarding_queue);
    bsal_map_destroy(&actor->acquaintance_map);

    bsal_map_destroy(&actor->received_messages);
    bsal_map_destroy(&actor->sent_messages);

    while (bsal_queue_dequeue(&actor->enqueued_messages, &message)) {
        buffer = bsal_message_buffer(&message);

        if (buffer != NULL) {
            bsal_memory_free(buffer);
        }
    }

    bsal_queue_destroy(&actor->enqueued_messages);

    actor->name = -1;
    actor->dead = 1;

    bsal_actor_unpin_from_worker(actor);

    actor->script = NULL;
    actor->worker = NULL;
    actor->state = NULL;

    /* unlock the actor if the actor is being destroyed while
     * being locked
     */
    bsal_actor_unlock(actor);

    bsal_lock_destroy(&actor->receive_lock);

    /* when exiting the destructor, the actor is unlocked
     * and destroyed too
     */
}

int bsal_actor_name(struct bsal_actor *actor)
{
    return actor->name;
}

void *bsal_actor_concrete_actor(struct bsal_actor *actor)
{
    return actor->state;
}

bsal_actor_receive_fn_t bsal_actor_get_receive(struct bsal_actor *actor)
{
    return bsal_script_get_receive(actor->script);
}

void bsal_actor_set_name(struct bsal_actor *actor, int name)
{
    actor->name = name;
}

void bsal_actor_print(struct bsal_actor *actor)
{
    /* with -Werror -Wall:
     * engine/bsal_actor.c:58:21: error: ISO C for bids conversion of function pointer to object pointer type [-Werror=edantic]
     */

    int received = (int)bsal_counter_get(&actor->counter, BSAL_COUNTER_RECEIVED_MESSAGES);
    int sent = (int)bsal_counter_get(&actor->counter, BSAL_COUNTER_SENT_MESSAGES);

    printf("[bsal_actor_print] Name: %i Supervisor %i Node: %i, Thread: %i"
                    " received %i sent %i\n", bsal_actor_name(actor),
                    bsal_actor_supervisor(actor),
                    bsal_node_name(bsal_actor_node(actor)),
                    bsal_worker_name(bsal_actor_worker(actor)),
                    received, sent);
}

bsal_actor_init_fn_t bsal_actor_get_init(struct bsal_actor *actor)
{
    return bsal_script_get_init(actor->script);
}

bsal_actor_destroy_fn_t bsal_actor_get_destroy(struct bsal_actor *actor)
{
    return bsal_script_get_destroy(actor->script);
}

void bsal_actor_set_worker(struct bsal_actor *actor, struct bsal_worker *worker)
{
    actor->worker = worker;
}

int bsal_actor_send_system_self(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;

    tag = bsal_message_tag(message);

    if (tag == BSAL_ACTOR_PIN_TO_WORKER) {
        bsal_actor_pin_to_worker(actor);
        return 1;

    } else if (tag == BSAL_ACTOR_UNPIN_FROM_WORKER) {
        bsal_actor_unpin_from_worker(actor);
        return 1;

    } else if (tag == BSAL_ACTOR_PIN_TO_NODE) {
        bsal_actor_pin_to_node(actor);
        return 1;

    } else if (tag == BSAL_ACTOR_UNPIN_FROM_NODE) {
        bsal_actor_unpin_from_node(actor);
        return 1;

    } else if (tag == BSAL_ACTOR_PACK_ENABLE) {

            /*
        printf("DEBUG actor %d enabling can_pack\n",
                        bsal_actor_name(actor));
                        */

        actor->can_pack = BSAL_ACTOR_STATUS_SUPPORTED;
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_UNPIN_FROM_WORKER);
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_UNPIN_FROM_NODE);

        return 1;

    } else if (tag == BSAL_ACTOR_PACK_DISABLE) {
        actor->can_pack = BSAL_ACTOR_STATUS_NOT_SUPPORTED;
        return 1;

    } else if (tag == BSAL_ACTOR_YIELD) {
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_YIELD_REPLY);
        return 1;

    } else if (tag == BSAL_ACTOR_STOP) {

        bsal_actor_die(actor);
        return 1;
    }

    return 0;
}

int bsal_actor_send_system(struct bsal_actor *actor, int name, struct bsal_message *message)
{
    int self;

    self = bsal_actor_name(actor);

    /* Verify if the message is a special message.
     * For instance, it is important to pin an
     * actor right away if it is requested.
     */
    if (name == self) {
        if (bsal_actor_send_system_self(actor, message)) {
            return 1;
        }
    }

    return 0;
}

void bsal_actor_send(struct bsal_actor *actor, int name, struct bsal_message *message)
{
    int source;
    source = bsal_actor_name(actor);

    /* update counters
     */
    if (source == name) {
        bsal_counter_add(&actor->counter, BSAL_COUNTER_SENT_MESSAGES_TO_SELF, 1);
        bsal_counter_add(&actor->counter, BSAL_COUNTER_SENT_BYTES_TO_SELF,
                        bsal_message_count(message));
    } else {
        bsal_counter_add(&actor->counter, BSAL_COUNTER_SENT_MESSAGES_NOT_TO_SELF, 1);
        bsal_counter_add(&actor->counter, BSAL_COUNTER_SENT_BYTES_NOT_TO_SELF,
                        bsal_message_count(message));
    }

    if (bsal_actor_send_system(actor, name, message)) {
        return;
    }

    bsal_actor_send_with_source(actor, name, message, source);
}

void bsal_actor_send_with_source(struct bsal_actor *actor, int name, struct bsal_message *message,
                int source)
{
    bsal_message_set_source(message, source);
    bsal_message_set_destination(message, name);

#ifdef BSAL_ACTOR_DEBUG9
    if (bsal_message_tag(message) == 1100) {
        printf("DEBUG bsal_message_set_source 1100\n");
    }
#endif

    /* messages sent in the init or destroy are not sent
     * at all !
     */
    if (actor->worker == NULL) {
        return;
    }

    bsal_worker_send(actor->worker, message);
}

int bsal_actor_spawn(struct bsal_actor *actor, int script)
{
    int name;

    name = bsal_actor_spawn_real(actor, script);

    bsal_actor_add_child(actor, name);

#ifdef BSAL_ACTOR_DEBUG_SPAWN
    printf("acquaintances after spawning\n");
    bsal_vector_helper_print_int(&actor->acquaintance_vector);
    printf("\n");
#endif

    return name;
}

int bsal_actor_spawn_real(struct bsal_actor *actor, int script)
{
    int name;
    int self_name = bsal_actor_name(actor);

#ifdef BSAL_ACTOR_DEBUG_SPAWN
    printf("DEBUG bsal_actor_spawn script %d\n", script);
#endif

    name = bsal_node_spawn(bsal_actor_node(actor), script);

    if (name == BSAL_ACTOR_NOBODY) {
        printf("Error: problem with spawning! did you register the script ?\n");
        return name;
    }

#ifdef BSAL_ACTOR_DEBUG_SPAWN
    printf("DEBUG bsal_actor_spawn before set_supervisor, spawned %d\n",
                    name);
#endif

    bsal_node_set_supervisor(bsal_actor_node(actor), name, self_name);

    bsal_counter_add(&actor->counter, BSAL_COUNTER_SPAWNED_ACTORS, 1);

    return name;
}

struct bsal_worker *bsal_actor_worker(struct bsal_actor *actor)
{
    return actor->worker;
}

int bsal_actor_dead(struct bsal_actor *actor)
{
    return actor->dead;
}

void bsal_actor_die(struct bsal_actor *actor)
{
    bsal_counter_add(&actor->counter, BSAL_COUNTER_KILLED_ACTORS, 1);
    actor->dead = 1;
}

struct bsal_counter *bsal_actor_counter(struct bsal_actor *actor)
{
    return &actor->counter;
}

struct bsal_node *bsal_actor_node(struct bsal_actor *actor)
{
    if (actor->node != NULL) {
        return actor->node;
    }

    if (actor->worker == NULL) {
        return NULL;
    }

    return bsal_worker_node(bsal_actor_worker(actor));
}

void bsal_actor_lock(struct bsal_actor *actor)
{
    bsal_lock_lock(&actor->receive_lock);
    actor->locked = 1;
}

void bsal_actor_unlock(struct bsal_actor *actor)
{
    if (!actor->locked) {
        return;
    }

    actor->locked = 0;
    bsal_lock_unlock(&actor->receive_lock);
}

int bsal_actor_argc(struct bsal_actor *actor)
{
    return bsal_node_argc(bsal_actor_node(actor));
}

char **bsal_actor_argv(struct bsal_actor *actor)
{
    return bsal_node_argv(bsal_actor_node(actor));
}

void bsal_actor_pin_to_worker(struct bsal_actor *actor)
{
    actor->affinity_worker = actor->worker;
}

struct bsal_worker *bsal_actor_affinity_worker(struct bsal_actor *actor)
{
    return actor->affinity_worker;
}

void bsal_actor_unpin_from_worker(struct bsal_actor *actor)
{
    actor->affinity_worker = NULL;
}

int bsal_actor_supervisor(struct bsal_actor *actor)
{
    return bsal_vector_helper_at_as_int(&actor->acquaintance_vector,
                    BSAL_ACTOR_ACQUAINTANCE_SUPERVISOR);
}

void bsal_actor_set_supervisor(struct bsal_actor *actor, int supervisor)
{
    if (bsal_vector_size(&actor->acquaintance_vector) == 0) {
        bsal_vector_push_back(&actor->acquaintance_vector, &supervisor);
    } else {
        bsal_vector_helper_set_int(&actor->acquaintance_vector, BSAL_ACTOR_ACQUAINTANCE_SUPERVISOR,
                        supervisor);
    }
}

int bsal_actor_receive_system_no_pack(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;

    tag = bsal_message_tag(message);

    if (tag == BSAL_ACTOR_PACK) {

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_PACK_REPLY);
        return 1;

    } else if (tag == BSAL_ACTOR_PACK_SIZE) {
        bsal_actor_helper_send_reply_int(actor, BSAL_ACTOR_PACK_SIZE_REPLY, 0);
        return 1;

    } else if (tag == BSAL_ACTOR_UNPACK) {
        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_PACK_REPLY);
        return 1;

    } else if (tag == BSAL_ACTOR_CLONE) {

        /* return nothing if the cloning is not supported or
         * if a cloning is already in progress, the message will be queued below.
         */

            /*
        printf("DEBUG actor %d BSAL_ACTOR_CLONE not supported can_pack %d\n", bsal_actor_name(actor),
                        actor->can_pack);
                        */

        bsal_actor_helper_send_reply_int(actor, BSAL_ACTOR_CLONE_REPLY, BSAL_ACTOR_NOBODY);
        return 1;

    } else if (tag == BSAL_ACTOR_MIGRATE) {

        /* return nothing if the cloning is not supported or
         * if a cloning is already in progress
         */

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate: pack not supported\n");
#endif

        bsal_actor_helper_send_reply_int(actor, BSAL_ACTOR_MIGRATE_REPLY, BSAL_ACTOR_NOBODY);

        return 1;
    }

    return 0;
}

int bsal_actor_receive_system(struct bsal_actor *actor, struct bsal_message *message)
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
    struct bsal_message new_message;
    int offset;
    int bytes;

    tag = bsal_message_tag(message);

    /* the concrete actor must catch these otherwise.
     * Also, clone and migrate depend on these.
     */
    if (actor->can_pack == BSAL_ACTOR_STATUS_NOT_SUPPORTED) {

        if (bsal_actor_receive_system_no_pack(actor, message)) {
            return 1;
        }
    }

    name = bsal_actor_name(actor);
    source =bsal_message_source(message);
    buffer = bsal_message_buffer(message);
    count = bsal_message_count(message);

    /* check message tags that are required for migration
     * before attempting to queue messages during hot actor
     * migration
     */

    /* cloning workflow in 4 easy steps !
     */
    if (tag == BSAL_ACTOR_CLONE) {

        if (actor->cloning_status == BSAL_ACTOR_STATUS_NOT_STARTED) {

            /* begin the cloning operation */
            bsal_actor_clone(actor, message);

        } else {
            /* queue the cloning message */
#ifdef BSAL_ACTOR_DEBUG_MIGRATE
            printf("DEBUG bsal_actor_receive_system queuing message %d because cloning is in progress\n",
                            tag);
#endif

            actor->forwarding_selector = BSAL_ACTOR_FORWARDING_CLONE;
            bsal_actor_queue_message(actor, message);
        }

        return 1;

    } else if (actor->cloning_status == BSAL_ACTOR_STATUS_STARTED) {

        /* call a function called
         * bsal_actor_continue_clone
         */
        actor->cloning_progressed = 0;
        bsal_actor_continue_clone(actor, message);

        if (actor->cloning_progressed) {
            return 1;
        }
    }

    if (actor->migration_status == BSAL_ACTOR_STATUS_STARTED) {

        actor->migration_progressed = 0;
        bsal_actor_migrate(actor, message);

        if (actor->migration_progressed) {
            return 1;
        }
    }

    /* spawn an actor.
     * This works even during migration because the supervisor is the
     * source of BSAL_ACTOR_SPAWN...
     */
    if (tag == BSAL_ACTOR_SPAWN) {
        script = *(int *)buffer;
        spawned = bsal_actor_spawn_real(actor, script);
        bsal_node_set_supervisor(bsal_actor_node(actor), spawned, source);

        new_buffer = bsal_memory_allocate(2 * sizeof(int));
        offset = 0;

        bytes = sizeof(spawned);
        memcpy((char *)new_buffer + offset, &spawned, bytes);
        offset += bytes;
        bytes = sizeof(script);
        memcpy((char *)new_buffer + offset, &script, bytes);
        offset += bytes;

        new_count = offset;
        bsal_message_init(&new_message, BSAL_ACTOR_SPAWN_REPLY, new_count, new_buffer);
        bsal_actor_send(actor, source, &new_message);

        bsal_message_destroy(&new_message);
        bsal_memory_free(new_buffer);

        return 1;

    } else if (tag == BSAL_ACTOR_SPAWN_REPLY) {

        name = *(int *)buffer;
        bsal_actor_add_child(actor, name);

    } else if (tag == BSAL_ACTOR_FORWARD_MESSAGES) {

        /* the dispatcher can handle this one
         */
        return 0;

    } else if (tag == BSAL_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES) {
        bsal_actor_migrate_notify_acquaintances(actor, message);
        return 1;

    } else if (tag == BSAL_ACTOR_NOTIFY_NAME_CHANGE) {

        bsal_actor_notify_name_change(actor, message);
        return 1;

    } else if (tag == BSAL_ACTOR_NOTIFY_NAME_CHANGE_REPLY) {

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES);

        return 1;

    } else if (tag == BSAL_ACTOR_PACK && source == name) {
        /* BSAL_ACTOR_PACK has to go through during live migration
         */
        return 0;

    } else if (tag == BSAL_ACTOR_PROXY_MESSAGE) {
        bsal_actor_receive_proxy_message(actor, message);
        return 1;

    } else if (tag == BSAL_ACTOR_FORWARD_MESSAGES_REPLY) {
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
     * BSAL_ACTOR_CLONE messsages are also queued during cloning...
     */
    if (actor->migration_status == BSAL_ACTOR_STATUS_STARTED) {

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_receive_system queuing message %d during migration\n",
                        tag);
#endif
        actor->forwarding_selector = BSAL_ACTOR_FORWARDING_MIGRATE;
        bsal_actor_queue_message(actor, message);
        return 1;
    }


    /* Perform binomial routing.
     */
    if (tag == BSAL_ACTOR_BINOMIAL_TREE_SEND) {
        bsal_actor_helper_receive_binomial_tree_send(actor, message);
        return 1;

    } else if (tag == BSAL_ACTOR_MIGRATE) {
        bsal_actor_migrate(actor, message);
        return 1;

    } else if (tag == BSAL_ACTOR_SYNCHRONIZE) {
        /* the concrete actor must catch this one */

    } else if (tag == BSAL_ACTOR_SYNCHRONIZE_REPLY) {
        bsal_actor_receive_synchronize_reply(actor, message);

        /* we also allow the concrete actor to receive this */

    /* Ignore BSAL_ACTOR_PIN and BSAL_ACTOR_UNPIN
     * because they can only be sent by an actor
     * to itself.
     */
    } else if (tag == BSAL_ACTOR_PIN_TO_WORKER) {
        return 1;

    } else if (tag == BSAL_ACTOR_UNPIN_FROM_WORKER) {
        return 1;

    } else if (tag == BSAL_ACTOR_PIN_TO_NODE) {
        return 1;

    } else if (tag == BSAL_ACTOR_UNPIN_FROM_NODE) {
        return 1;

    } else if (tag == BSAL_ACTOR_SET_SUPERVISOR
                    /*&& source == bsal_actor_supervisor(actor)*/) {

    /* only an actor that knows the name of
     * the current supervisor can assign a new supervisor
     * this information can not be obtained by default
     * for security reasons.
     */

        if (count != 2 * sizeof(int)) {
            return 1;
        }

        bsal_message_helper_unpack_int(message, 0, &old_supervisor);
        bsal_message_helper_unpack_int(message, sizeof(old_supervisor), &supervisor);

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_receive_system actor %d receives BSAL_ACTOR_SET_SUPERVISOR old supervisor %d (provided %d), new supervisor %d\n",
                        bsal_actor_name(actor),
                        bsal_actor_supervisor(actor), old_supervisor,
                        supervisor);
#endif

        if (bsal_actor_supervisor(actor) == old_supervisor) {

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
            printf("DEBUG bsal_actor_receive_system authentification successful\n");
#endif
            bsal_actor_set_supervisor(actor, supervisor);
        }

        bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_SET_SUPERVISOR_REPLY);

        return 1;

    /* BSAL_ACTOR_SYNCHRONIZE can not be queued.
     */
    } else if (tag == BSAL_ACTOR_SYNCHRONIZE) {
        return 1;

    /* BSAL_ACTOR_SYNCHRONIZED can only be sent to an actor
     * by itself.
     */
    } else if (tag == BSAL_ACTOR_SYNCHRONIZED && name != source) {
        return 1;

    /* block BSAL_ACTOR_STOP if it is not from self
     * acquaintance actors have to use BSAL_ACTOR_ASK_TO_STOP
     * in fact, this message has to be sent by the actor
     * itself.
     */
    } else if (tag == BSAL_ACTOR_STOP) {

        return 1;

    } else if (tag == BSAL_ACTOR_GET_NODE_NAME) {

        bsal_actor_helper_send_reply_int(actor, BSAL_ACTOR_GET_NODE_NAME_REPLY,
                        bsal_actor_node_name(actor));
        return 1;

    } else if (tag == BSAL_ACTOR_GET_NODE_WORKER_COUNT) {

        bsal_actor_helper_send_reply_int(actor, BSAL_ACTOR_GET_NODE_WORKER_COUNT_REPLY,
                        bsal_actor_node_worker_count(actor));
        return 1;
    }

    return 0;
}

void bsal_actor_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    bsal_actor_receive_fn_t receive;
    int name;
    int source;
    int *bucket;

#ifdef BSAL_ACTOR_DEBUG_SYNC
    printf("\nDEBUG bsal_actor_receive...... tag %d\n",
                    bsal_message_tag(message));

    if (bsal_message_tag(message) == BSAL_ACTOR_SYNCHRONIZED) {
        printf("DEBUG =============\n");
        printf("DEBUG bsal_actor_receive before concrete receive BSAL_ACTOR_SYNCHRONIZED\n");
    }

    printf("DEBUG bsal_actor_receive tag %d for %d\n",
                    bsal_message_tag(message),
                    bsal_actor_name(actor));
#endif

    source = bsal_message_source(message);

    actor->current_source = source;
    bucket = (int *)bsal_map_get(&actor->received_messages, &source);

    if (bucket == NULL) {
        bucket = (int *)bsal_map_add(&actor->received_messages, &source);
    }

    (*bucket)++;

    /* check if this is a message that the system can
     * figure out what to do with it
     */
    if (bsal_actor_receive_system(actor, message)) {
        return;

    /* otherwise, verify if the actor registered a
     * handler for this tag
     */
    } else if (bsal_actor_dispatch(actor, message)) {
        return;
    }

    /* Otherwise, this is a message for the actor itself.
     */
    receive = bsal_actor_get_receive(actor);

#ifdef BSAL_ACTOR_DEBUG_SYNC
    printf("DEBUG bsal_actor_receive calls concrete receive tag %d\n",
                    bsal_message_tag(message));
#endif

    name = bsal_actor_name(actor);

    /* update counters
     */
    if (source == name) {
        bsal_counter_add(&actor->counter, BSAL_COUNTER_RECEIVED_MESSAGES_FROM_SELF, 1);
        bsal_counter_add(&actor->counter, BSAL_COUNTER_RECEIVED_BYTES_FROM_SELF,
                        bsal_message_count(message));
    } else {
        bsal_counter_add(&actor->counter, BSAL_COUNTER_RECEIVED_MESSAGES_NOT_FROM_SELF, 1);
        bsal_counter_add(&actor->counter, BSAL_COUNTER_RECEIVED_BYTES_NOT_FROM_SELF,
                        bsal_message_count(message));
    }

    receive(actor, message);
}

void bsal_actor_receive_proxy_message(struct bsal_actor *actor,
                struct bsal_message *message)
{
    int source;

    source = bsal_actor_unpack_proxy_message(actor, message);
    bsal_actor_send_with_source(actor, bsal_actor_name(actor),
                    message, source);
}

void bsal_actor_receive_synchronize(struct bsal_actor *actor,
                struct bsal_message *message)
{
#ifdef BSAL_ACTOR_DEBUG
    printf("DEBUG56 replying to %i with BSAL_ACTOR_PRIVATE_SYNCHRONIZE_REPLY\n",
                    bsal_message_source(message));
#endif

    bsal_message_init(message, BSAL_ACTOR_SYNCHRONIZE_REPLY, 0, NULL);
    bsal_actor_send(actor, bsal_message_source(message), message);
}

void bsal_actor_receive_synchronize_reply(struct bsal_actor *actor,
                struct bsal_message *message)
{
    int name;

    if (actor->synchronization_started) {

#ifdef BSAL_ACTOR_DEBUG
        printf("DEBUG99 synchronization_reply %i/%i\n",
                        actor->synchronization_responses,
                        actor->synchronization_expected_responses);
#endif

        actor->synchronization_responses++;

        /* send BSAL_ACTOR_SYNCHRONIZED to self
         */
        if (bsal_actor_synchronization_completed(actor)) {

#ifdef BSAL_ACTOR_DEBUG_SYNC
            printf("DEBUG sending BSAL_ACTOR_SYNCHRONIZED to self\n");
#endif
            struct bsal_message new_message;
            bsal_message_init(&new_message, BSAL_ACTOR_SYNCHRONIZED,
                            sizeof(actor->synchronization_responses),
                            &actor->synchronization_responses);

            name = bsal_actor_name(actor);

            bsal_actor_send(actor, name, &new_message);
            actor->synchronization_started = 0;
        }
    }
}

void bsal_actor_synchronize(struct bsal_actor *actor, struct bsal_vector *actors)
{
    struct bsal_message message;

    actor->synchronization_started = 1;
    actor->synchronization_expected_responses = bsal_vector_size(actors);
    actor->synchronization_responses = 0;

    /* emit synchronization
     */

#ifdef BSAL_ACTOR_DEBUG
    printf("DEBUG actor %i emit synchronization %i-%i, expected: %i\n",
                    bsal_actor_name(actor), first, last,
                    actor->synchronization_expected_responses);
#endif

    bsal_message_init(&message, BSAL_ACTOR_SYNCHRONIZE, 0, NULL);

    /* TODO bsal_actor_send_range_binomial_tree is broken */
    bsal_actor_helper_send_range(actor, actors, &message);
    bsal_message_destroy(&message);
}

int bsal_actor_synchronization_completed(struct bsal_actor *actor)
{
    if (actor->synchronization_started == 0) {
        return 0;
    }

#ifdef BSAL_ACTOR_DEBUG
    printf("DEBUG32 actor %i bsal_actor_synchronization_completed %i/%i\n",
                    bsal_actor_name(actor),
                    actor->synchronization_responses,
                    actor->synchronization_expected_responses);
#endif

    if (actor->synchronization_responses == actor->synchronization_expected_responses) {
        return 1;
    }

    return 0;
}

int bsal_actor_unpack_proxy_message(struct bsal_actor *actor,
                struct bsal_message *message)
{
    int new_count;
    int tag;
    int source;
    void *buffer;
    int offset;

    buffer = bsal_message_buffer(message);
    new_count = bsal_message_count(message);
    new_count -= sizeof(source);
    new_count -= sizeof(tag);

    offset = new_count;

    source = *(int *)((char *)buffer + offset);
    offset += sizeof(source);
    tag = *(int *)((char *)buffer + offset);
    offset += sizeof(tag);

    /* TODO, verify if it is new_count or old count
     */
    bsal_message_init(message, tag, new_count, buffer);

    return source;
}

void bsal_actor_pack_proxy_message(struct bsal_actor *actor, struct bsal_message *message,
                int real_source)
{
    int real_tag;
    int count;
    int new_count;
    void *buffer;
    void *new_buffer;
    int offset;

    real_tag = bsal_message_tag(message);
    buffer = bsal_message_buffer(message);
    count = bsal_message_count(message);

    new_count = count + sizeof(real_source) + sizeof(real_tag);

    /* TODO: use slab allocator */
    new_buffer = bsal_memory_allocate(new_count);

#ifdef BSAL_ACTOR_DEBUG
    printf("DEBUG12 bsal_memory_allocate %p (pack proxy message)\n",
                    new_buffer);
#endif

    memcpy(new_buffer, buffer, count);

    offset = count;
    memcpy((char *)new_buffer + offset, &real_source, sizeof(real_source));
    offset += sizeof(real_source);
    memcpy((char *)new_buffer + offset, &real_tag, sizeof(real_tag));
    offset += sizeof(real_tag);

    bsal_message_init(message, BSAL_ACTOR_PROXY_MESSAGE, new_count, new_buffer);
    bsal_message_set_source(message, real_source);

    /* free the old buffer
     */
    bsal_memory_free(buffer);
    buffer = NULL;
}

int bsal_actor_script(struct bsal_actor *actor)
{
    return bsal_script_name(actor->script);
}

void bsal_actor_add_script(struct bsal_actor *actor, int name, struct bsal_script *script)
{
    bsal_node_add_script(bsal_actor_node(actor), name, script);
}

void bsal_actor_send_reply(struct bsal_actor *actor, struct bsal_message *message)
{
    bsal_actor_send(actor, bsal_actor_source(actor), message);
}

void bsal_actor_send_to_self(struct bsal_actor *actor, struct bsal_message *message)
{
    bsal_actor_send(actor, bsal_actor_name(actor), message);
}

void bsal_actor_send_to_supervisor(struct bsal_actor *actor, struct bsal_message *message)
{
    bsal_actor_send(actor, bsal_actor_supervisor(actor), message);
}

void bsal_actor_clone(struct bsal_actor *actor, struct bsal_message *message)
{
    int spawner;
    void *buffer;
    int script;
    struct bsal_message new_message;
    int source;

    script = bsal_actor_script(actor);
    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);
    spawner = *(int *)buffer;
    actor->cloning_spawner = spawner;
    actor->cloning_client = source;

#ifdef BSAL_ACTOR_DEBUG_CLONE
    int name;
    name = bsal_actor_name(actor);
    printf("DEBUG %d sending BSAL_ACTOR_SPAWN to spawner %d for client %d\n", name, spawner,
                    source);
#endif

    bsal_message_init(&new_message, BSAL_ACTOR_SPAWN, sizeof(script), &script);
    bsal_actor_send(actor, spawner, &new_message);

    actor->cloning_status = BSAL_ACTOR_STATUS_STARTED;
}

void bsal_actor_continue_clone(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;
    int source;
    int self;
    struct bsal_message new_message;
    int count;
    void *buffer;

    count = bsal_message_count(message);
    buffer = bsal_message_buffer(message);
    self = bsal_actor_name(actor);
    tag = bsal_message_tag(message);
    source = bsal_message_source(message);

#ifdef BSAL_ACTOR_DEBUG_CLONE1
    printf("DEBUG bsal_actor_continue_clone source %d tag %d\n", source, tag);
#endif

    if (tag == BSAL_ACTOR_SPAWN_REPLY && source == actor->cloning_spawner) {

#ifdef BSAL_ACTOR_DEBUG_CLONE
        printf("DEBUG bsal_actor_continue_clone BSAL_ACTOR_SPAWN_REPLY\n");
#endif

        actor->cloning_new_actor = *(int *)buffer;
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_PACK);

        actor->cloning_progressed = 1;

    } else if (tag == BSAL_ACTOR_PACK_REPLY && source == self) {

#ifdef BSAL_ACTOR_DEBUG_CLONE
        printf("DEBUG bsal_actor_continue_clone BSAL_ACTOR_PACK_REPLY\n");
#endif

        /* forward the buffer to the new actor */
        bsal_message_init(&new_message, BSAL_ACTOR_UNPACK, count, buffer);
        bsal_actor_send(actor, actor->cloning_new_actor, &new_message);

        actor->cloning_progressed = 1;

    } else if (tag == BSAL_ACTOR_UNPACK_REPLY && source == actor->cloning_new_actor) {

            /*
    } else if (tag == BSAL_ACTOR_FORWARD_MESSAGES_REPLY) {
#ifdef BSAL_ACTOR_DEBUG_CLONE
        printf("DEBUG bsal_actor_continue_clone BSAL_ACTOR_UNPACK_REPLY\n");
#endif
*/
        /* it is required that the cloning process be concluded at this point because otherwise
         * queued messages will be queued when they are being forwarded.
         */
        bsal_message_init(&new_message, BSAL_ACTOR_CLONE_REPLY, sizeof(actor->cloning_new_actor),
                        &actor->cloning_new_actor);
        bsal_actor_send(actor, actor->cloning_client, &new_message);

        /* we are ready for another cloning */
        actor->cloning_status = BSAL_ACTOR_STATUS_NOT_STARTED;

#ifdef BSAL_ACTOR_DEBUG_CLONE
        printf("actor:%d sends clone %d to client %d\n", bsal_actor_name(actor),
                        actor->cloning_new_actor, actor->cloning_client);
#endif

        actor->forwarding_selector = BSAL_ACTOR_FORWARDING_CLONE;

#ifdef BSAL_ACTOR_DEBUG_CLONE
        printf("DEBUG clone finished, forwarding queued messages (if any) to %d, queue/%d\n",
                        bsal_actor_name(actor), actor->forwarding_selector);
#endif

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_FORWARD_MESSAGES);

        actor->cloning_progressed = 1;
    }
}

int bsal_actor_source(struct bsal_actor *actor)
{
    return actor->current_source;
}

void bsal_actor_send_to_supervisor_int(struct bsal_actor *actor, int tag, int value)
{
    bsal_actor_helper_send_int(actor, bsal_actor_supervisor(actor), tag, value);
}

void bsal_actor_send_to_self_int(struct bsal_actor *actor, int tag, int value)
{
    bsal_actor_helper_send_int(actor, bsal_actor_name(actor), tag, value);
}

int bsal_actor_node_name(struct bsal_actor *actor)
{
    return bsal_node_name(bsal_actor_node(actor));
}

int bsal_actor_node_worker_count(struct bsal_actor *actor)
{
    return bsal_node_worker_count(bsal_actor_node(actor));
}

int bsal_actor_dispatch(struct bsal_actor *actor, struct bsal_message *message)
{

#ifdef BSAL_ACTOR_DEBUG_10335
    if (bsal_message_tag(message) == 10335) {
        printf("DEBUG actor %d bsal_actor_dispatch 10335\n",
                        bsal_actor_name(actor));
    }
#endif

    return bsal_dispatcher_dispatch(&actor->dispatcher, actor, message);
}

void bsal_actor_register(struct bsal_actor *actor, int tag, bsal_actor_receive_fn_t handler)
{

#ifdef BSAL_ACTOR_DEBUG_10335
    if (tag == 10335) {
        printf("DEBUG actor %d bsal_actor_register 10335\n",
                        bsal_actor_name(actor));
    }
#endif

    bsal_dispatcher_register(&actor->dispatcher, tag, handler);
}

struct bsal_dispatcher *bsal_actor_dispatcher(struct bsal_actor *actor)
{
    return &actor->dispatcher;
}

void bsal_actor_set_node(struct bsal_actor *actor, struct bsal_node *node)
{
    actor->node = node;
}

void bsal_actor_migrate(struct bsal_actor *actor, struct bsal_message *message)
{
    int spawner;
    void *buffer;
    int source;
    int tag;
    int name;
    struct bsal_message new_message;
    int data[2];
    int selector;

    tag = bsal_message_tag(message);
    source = bsal_message_source(message);
    name = bsal_actor_name(actor);

    if (actor->migration_cloned == 0) {

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate bsal_actor_migrate: cloning self...\n");
#endif

        /* clone self
         */
        source = bsal_message_source(message);
        buffer = bsal_message_buffer(message);
        spawner = *(int *)buffer;
        name = bsal_actor_name(actor);

        actor->migration_spawner = spawner;
        actor->migration_client = source;

        bsal_actor_send_to_self_int(actor, BSAL_ACTOR_CLONE, spawner);

        actor->migration_status = BSAL_ACTOR_STATUS_STARTED;
        actor->migration_cloned = 1;

        actor->migration_progressed = 1;

    } else if (tag == BSAL_ACTOR_CLONE_REPLY && source == name) {

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate bsal_actor_migrate: cloned.\n");
#endif

        /* tell acquaintances that the clone is the new original.
         */
        bsal_message_helper_unpack_int(message, 0, &actor->migration_new_actor);

        actor->acquaintance_index = 0;
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES);

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate: notify acquaintance of name change.\n");
#endif
        actor->migration_progressed = 1;

    } else if (tag == BSAL_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES_REPLY && source == name) {

        /* at this point, there should not be any new messages addressed
         * to the old name if all the code implied uses
         * acquaintance vectors.
         */
        /* assign the supervisor of the original version
         * of the migrated actor to the new version
         * of the migrated actor
         */
#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate actor %d setting supervisor of %d to %d\n",
                        bsal_actor_name(actor),
                        actor->migration_new_actor,
                        bsal_actor_supervisor(actor));
#endif

        data[0] = bsal_actor_name(actor);
        data[1] = bsal_actor_supervisor(actor);

        bsal_message_init(&new_message, BSAL_ACTOR_SET_SUPERVISOR,
                        2 * sizeof(int), data);
        bsal_actor_send(actor, actor->migration_new_actor, &new_message);
        bsal_message_destroy(&new_message);

        actor->migration_progressed = 1;

    } else if (tag == BSAL_ACTOR_FORWARD_MESSAGES_REPLY && actor->migration_forwarded_messages) {

        /* send the name of the new copy and die of a peaceful death.
         */
        bsal_actor_helper_send_int(actor, actor->migration_client, BSAL_ACTOR_MIGRATE_REPLY,
                        actor->migration_new_actor);

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_STOP);

        actor->migration_status = BSAL_ACTOR_STATUS_NOT_STARTED;

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
        printf("DEBUG bsal_actor_migrate: OK, now killing self and returning clone name to client.\n");
#endif

        actor->migration_progressed = 1;

    } else if (tag == BSAL_ACTOR_SET_SUPERVISOR_REPLY
                    && source == actor->migration_new_actor) {

        if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_NONE) {

            /* the forwarding system is ready to be used.
             */
            actor->forwarding_selector = BSAL_ACTOR_FORWARDING_MIGRATE;

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
            printf("DEBUG bsal_actor_migrate %d forwarding queued messages to actor %d, queue/%d (forwarding system ready.)\n",
                        bsal_actor_name(actor),
                        actor->migration_new_actor, actor->forwarding_selector);
#endif

            bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_FORWARD_MESSAGES);
            actor->migration_forwarded_messages = 1;

        /* wait for the clone queue to be empty.
         */
        } else {

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
            printf("DEBUG bsal_actor_migrate queuing system is busy (used by queue/%d), queuing selector\n",
                            actor->forwarding_selector);
#endif
            /* queue the selector into the forwarding system
             */
            selector = BSAL_ACTOR_FORWARDING_MIGRATE;
            bsal_queue_enqueue(&actor->forwarding_queue, &selector);
        }

        actor->migration_progressed = 1;
    }
}

void bsal_actor_notify_name_change(struct bsal_actor *actor, struct bsal_message *message)
{
    int old_name;
    int new_name;
    int source;
    int index;
    int *bucket;
    struct bsal_message new_message;
    int enqueued_messages;

    source = bsal_message_source(message);
    old_name = source;
    bsal_message_helper_unpack_int(message, 0, &new_name);

    /* update the acquaintance vector
     */
    index = bsal_actor_get_acquaintance_index(actor, old_name);

    bucket = bsal_vector_at(&actor->acquaintance_vector, index);
    *bucket = new_name;

    /* update userland queued messages
     */
    enqueued_messages = bsal_actor_enqueued_message_count(actor);

    while (enqueued_messages--) {

        bsal_actor_dequeue_message(actor, &new_message);
        if (bsal_message_source(&new_message) == old_name) {
            bsal_message_set_source(&new_message, new_name);
        }
        bsal_actor_enqueue_message(actor, &new_message);
    }

    bsal_actor_helper_send_reply_empty(actor, BSAL_ACTOR_NOTIFY_NAME_CHANGE_REPLY);
}

struct bsal_vector *bsal_actor_acquaintance_vector(struct bsal_actor *actor)
{
    return &actor->acquaintance_vector;
}

void bsal_actor_migrate_notify_acquaintances(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_vector *acquaintance_vector;
    int acquaintance;

    acquaintance_vector = bsal_actor_acquaintance_vector(actor);

    if (actor->acquaintance_index < bsal_vector_size(acquaintance_vector)) {

        acquaintance = bsal_vector_helper_at_as_int(acquaintance_vector, actor->acquaintance_index);
        bsal_actor_helper_send_int(actor, acquaintance, BSAL_ACTOR_NOTIFY_NAME_CHANGE,
                        actor->migration_new_actor);
        actor->acquaintance_index++;

    } else {

        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES_REPLY);
    }
}

void bsal_actor_send_to_self_proxy(struct bsal_actor *actor,
                struct bsal_message *message, int real_source)
{
    int destination;

    destination = bsal_actor_name(actor);
    bsal_actor_send_proxy(actor, destination, message, real_source);
}

void bsal_actor_send_proxy(struct bsal_actor *actor, int destination,
                struct bsal_message *message, int real_source)
{
    struct bsal_message new_message;

    bsal_message_init_copy(&new_message, message);

    bsal_actor_pack_proxy_message(actor, &new_message, real_source);
    bsal_actor_send(actor, destination, &new_message);

    bsal_message_destroy(&new_message);
}

void bsal_actor_queue_message(struct bsal_actor *actor,
                struct bsal_message *message)
{
    void *buffer;
    void *new_buffer;
    int count;
    struct bsal_message new_message;
    struct bsal_queue *queue;
    int tag;
    int source;

    bsal_message_helper_get_all(message, &tag, &count, &buffer, &source);

    new_buffer = NULL;

#ifdef BSAL_ACTOR_DEBUG_MIGRATE
    printf("DEBUG bsal_actor_queue_message queuing message tag= %d to queue queue/%d\n",
                        tag, actor->forwarding_selector);
#endif

    if (count > 0) {
        new_buffer = bsal_memory_allocate(count);
        memcpy(new_buffer, buffer, count);
    }

    bsal_message_init(&new_message, tag, count, new_buffer);
    bsal_message_set_source(&new_message,
                    bsal_message_source(message));
    bsal_message_set_destination(&new_message,
                    bsal_message_destination(message));

    queue = NULL;

    if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_CLONE) {
        queue = &actor->queued_messages_for_clone;
    } else if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_MIGRATE) {

        queue = &actor->queued_messages_for_migration;
    }

    bsal_queue_enqueue(queue, &new_message);
}

void bsal_actor_forward_messages(struct bsal_actor *actor, struct bsal_message *message)
{
    struct bsal_message new_message;
    struct bsal_queue *queue;
    int destination;
    void *buffer_to_release;

    queue = NULL;
    destination = -1;

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
    printf("DEBUG bsal_actor_forward_messages using queue/%d\n",
                    actor->forwarding_selector);
#endif

    if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_CLONE) {
        queue = &actor->queued_messages_for_clone;
        destination = bsal_actor_name(actor);

    } else if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_MIGRATE) {

        queue = &actor->queued_messages_for_migration;
        destination = actor->migration_new_actor;
    }

    if (queue == NULL) {

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
        printf("DEBUG bsal_actor_forward_messages error could not select queue\n");
#endif
        return;
    }

    if (bsal_queue_dequeue(queue, &new_message)) {

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
        printf("DEBUG bsal_actor_forward_messages actor %d forwarding message to actor %d tag is %d,"
                            " real source is %d\n",
                            bsal_actor_name(actor),
                            destination,
                            bsal_message_tag(&new_message),
                            bsal_message_source(&new_message));
#endif

        bsal_actor_pack_proxy_message(actor, &new_message,
                        bsal_message_source(&new_message));
        bsal_actor_send(actor, destination, &new_message);

        buffer_to_release = bsal_message_buffer(&new_message);
        bsal_memory_free(buffer_to_release);

        /* recursive actor call
         */
        bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_FORWARD_MESSAGES);
    } else {

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
        printf("DEBUG bsal_actor_forward_messages actor %d has no more messages to forward in queue/%d\n",
                        bsal_actor_name(actor), actor->forwarding_selector);
#endif

        if (bsal_queue_dequeue(&actor->forwarding_queue, &actor->forwarding_selector)) {

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
            printf("DEBUG bsal_actor_forward_messages will now using queue (FIFO pop)/%d\n",
                            actor->forwarding_selector);
#endif
            if (actor->forwarding_selector == BSAL_ACTOR_FORWARDING_MIGRATE) {
                actor->migration_forwarded_messages = 1;
            }

            /* do a recursive call
             */
            bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_FORWARD_MESSAGES);
        } else {

#ifdef BSAL_ACTOR_DEBUG_FORWARDING
            printf("DEBUG bsal_actor_forward_messages the forwarding system is now available.\n");
#endif

            actor->forwarding_selector = BSAL_ACTOR_FORWARDING_NONE;
            /* this is finished
             */
            bsal_actor_helper_send_to_self_empty(actor, BSAL_ACTOR_FORWARD_MESSAGES_REPLY);

        }
    }
}

void bsal_actor_pin_to_node(struct bsal_actor *actor)
{

}

void bsal_actor_unpin_from_node(struct bsal_actor *actor)
{

}

int bsal_actor_acquaintance_count(struct bsal_actor *actor)
{
    return bsal_vector_size(&actor->acquaintance_vector);
}

int bsal_actor_get_child(struct bsal_actor *actor, int index)
{
    int index2;

    if (index < bsal_vector_size(&actor->children)) {
        index2 = *(int *)bsal_vector_at(&actor->children, index);
        return bsal_actor_get_acquaintance(actor, index2);
    }

    return BSAL_ACTOR_NOBODY;
}

int bsal_actor_child_count(struct bsal_actor *actor)
{
    return bsal_vector_size(&actor->children);
}

int bsal_actor_add_child(struct bsal_actor *actor, int name)
{
    int index;

    index = bsal_actor_add_acquaintance(actor, name);
    bsal_vector_push_back(&actor->children, &index);

    return index;
}

int bsal_actor_add_acquaintance(struct bsal_actor *actor, int name)
{
    int index;
    int *bucket;

    index = bsal_actor_get_acquaintance_index(actor, name);

    if (index >= 0) {
        return index;
    }

    if (name == BSAL_ACTOR_NOBODY || name < 0) {
        return -1;
    }

    bsal_vector_helper_push_back_int(bsal_actor_acquaintance_vector(actor),
                    name);

    index = bsal_vector_size(bsal_actor_acquaintance_vector(actor)) - 1;

    bucket = bsal_map_add(&actor->acquaintance_map, &name);
    *bucket = index;

    return index;
}

int bsal_actor_get_acquaintance(struct bsal_actor *actor, int index)
{
    if (index < bsal_vector_size(bsal_actor_acquaintance_vector(actor))) {
        return bsal_vector_helper_at_as_int(bsal_actor_acquaintance_vector(actor),
                        index);
    }

    return BSAL_ACTOR_NOBODY;
}

int bsal_actor_get_acquaintance_index(struct bsal_actor *actor, int name)
{
    int *bucket;

#if 0
    return bsal_vector_index_of(&actor->acquaintance_vector, &name);
#endif

    bucket = bsal_map_get(&actor->acquaintance_map, &name);

    if (bucket == NULL) {
        return -1;
    }

    return *bucket;
}

int bsal_actor_get_child_index(struct bsal_actor *actor, int name)
{
    int i;
    int index;
    int child;

    if (name == BSAL_ACTOR_NOBODY) {
        return -1;
    }

    for (i = 0; i < bsal_actor_child_count(actor); i++) {
        index = *(int *)bsal_vector_at(&actor->children, i);

#ifdef BSAL_ACTOR_DEBUG_CHILDREN
        printf("DEBUG index %d\n", index);
#endif

        child = bsal_actor_get_acquaintance(actor, index);

        if (child == name) {
            return i;
        }
    }

    return -1;
}

void bsal_actor_enqueue_message(struct bsal_actor *actor, struct bsal_message *message)
{
    void *new_buffer;
    int count;
    void *buffer;
    int source;
    int tag;
    struct bsal_message new_message;
    int destination;

    bsal_message_helper_get_all(message, &tag, &count, &buffer, &source);
    destination = bsal_message_destination(message);

    new_buffer = NULL;

    if (buffer != NULL) {
        new_buffer = bsal_memory_allocate(count);
        memcpy(new_buffer, buffer, count);
    }

    bsal_message_init(&new_message, tag, count, new_buffer);
    bsal_message_set_source(&new_message, source);
    bsal_message_set_destination(&new_message, destination);

    bsal_queue_enqueue(&actor->enqueued_messages, &new_message);
    bsal_message_destroy(&new_message);
}

void bsal_actor_dequeue_message(struct bsal_actor *actor, struct bsal_message *message)
{
    if (bsal_actor_enqueued_message_count(actor) == 0) {
        return;
    }

    bsal_queue_dequeue(&actor->enqueued_messages, message);
}

int bsal_actor_enqueued_message_count(struct bsal_actor *actor)
{
    return bsal_queue_size(&actor->enqueued_messages);
}

struct bsal_map *bsal_actor_get_received_messages(struct bsal_actor *self)
{
    return &self->received_messages;
}

struct bsal_map *bsal_actor_get_sent_messages(struct bsal_actor *self)
{
    return &self->sent_messages;
}

struct bsal_memory_pool *bsal_actor_get_ephemeral_memory(struct bsal_actor *actor)
{
    struct bsal_worker *worker;

    worker = bsal_actor_worker(actor);

    if (worker == NULL) {
        return NULL;
    }

    return bsal_worker_get_ephemeral_memory(worker);
}
