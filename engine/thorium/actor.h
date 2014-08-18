
#ifndef THORIUM_ACTOR_H
#define THORIUM_ACTOR_H

#include "message.h"
#include "script.h"

#include "dispatcher.h"

#include <core/structures/vector.h>
#include <core/structures/map.h>
#include <core/structures/queue.h>
#include <core/structures/fast_ring.h>

#include <core/system/lock.h>
#include <core/system/counter.h>

#include <core/helpers/actor_helper.h>

#include <pthread.h>
#include <stdint.h>

/*
 * Expose the actor acquaintance API.
 *
 * This is not a good thing to do.
 */
/*
#define THORIUM_ACTOR_EXPOSE_ACQUAINTANCE_VECTOR
*/

/* for control */
#define THORIUM_ACTOR_START 0x00000885
#define THORIUM_ACTOR_START_REPLY 0x0000232f
#define THORIUM_ACTOR_STOP 0x0000388c
#define THORIUM_ACTOR_STOP_REPLY 0x00006fd8
#define THORIUM_ACTOR_ASK_TO_STOP 0x0000607b
#define THORIUM_ACTOR_ASK_TO_STOP_REPLY 0x00003602

#define THORIUM_ACTOR_SET_CONSUMER 0x000020a9
#define THORIUM_ACTOR_SET_CONSUMER_REPLY 0x00004db0

#define THORIUM_ACTOR_SET_PRODUCER 0x00002856
#define THORIUM_ACTOR_SET_PRODUCER_REPLY 0x00000037
#define THORIUM_ACTOR_SET_PRODUCERS 0x00005c0b
#define THORIUM_ACTOR_SET_PRODUCERS_REPLY 0x0000774d

#define THORIUM_ACTOR_SET_CONSUMERS 0x0000671d
#define THORIUM_ACTOR_SET_CONSUMERS_REPLY 0x000071e4

/* runtime info */
#define THORIUM_ACTOR_GET_NODE_NAME 0x00003323
#define THORIUM_ACTOR_GET_NODE_NAME_REPLY 0x00004d9a
#define THORIUM_ACTOR_GET_NODE_WORKER_COUNT 0x0000147d
#define THORIUM_ACTOR_GET_NODE_WORKER_COUNT_REPLY 0x000004ec

/* control YIELD is used as a yielding process.
 * an actor sends this to itself
 * when it receives THORIUM_ACTOR_YIELD_REPLY, it continues
 * its work
 */
#define THORIUM_ACTOR_YIELD 0x00000173
#define THORIUM_ACTOR_YIELD_REPLY 0x000016f1

/* binomial-tree */
#define THORIUM_ACTOR_BINOMIAL_TREE_SEND 0x00005b36
#define THORIUM_ACTOR_PROXY_MESSAGE 0x00004bed

/* affinity */
/*
#define THORIUM_ACTOR_PIN_TO_WORKER 0x000017a1
#define THORIUM_ACTOR_UNPIN_FROM_WORKER 0x00007b66
#define THORIUM_ACTOR_PIN_TO_NODE 0x00007b38
#define THORIUM_ACTOR_UNPIN_FROM_NODE 0x00006dab
*/

/* synchronization */
#define THORIUM_ACTOR_SYNCHRONIZE 0x00004ac9
#define THORIUM_ACTOR_SYNCHRONIZE_REPLY 0x00000663
#define THORIUM_ACTOR_SYNCHRONIZED 0x0000453d

/* spawn new actors remotely */
#define THORIUM_ACTOR_SPAWN 0x00000119
#define THORIUM_ACTOR_SPAWN_REPLY 0x00007b68

/* for import and export */
#define THORIUM_ACTOR_PACK_ENABLE 0x000015d3
#define THORIUM_ACTOR_PACK_DISABLE 0x00007f0f
#define THORIUM_ACTOR_PACK 0x00004cae
#define THORIUM_ACTOR_PACK_REPLY 0x000024fc
#define THORIUM_ACTOR_UNPACK 0x00001c73
#define THORIUM_ACTOR_UNPACK_REPLY 0x000064e4
#define THORIUM_ACTOR_PACK_SIZE 0x00003307
#define THORIUM_ACTOR_PACK_SIZE_REPLY 0x00005254

/* cloning */
/* design notes:

   clone a process

   THORIUM_ACTOR_CLONE
   THORIUM_ACTOR_REPLY (contains the clone name)

cloning_state: not supported, supported, started, finished

does this: THORIUM_ACTOR_SPAWN
THORIUM_ACTOR_PACK to self
forward THORIUM_ACTOR_PACK_REPLY to new spawnee as UNPACK
reply THORIUM_ CLONE_REPLY with newly spawned actor
*/

/*
 * CLONE takes one int (the spawner) and returns CLONE_REPLY
 *
 */
#define THORIUM_ACTOR_CLONE 0x00000d60
/* CLONE_REPLY returns one int: the clone name */
#define THORIUM_ACTOR_CLONE_REPLY 0x00006881

/* for migration */
#define THORIUM_ACTOR_MIGRATE 0x000073ff
#define THORIUM_ACTOR_MIGRATE_REPLY 0x00001442
#define THORIUM_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES 0x000029b6
#define THORIUM_ACTOR_MIGRATE_NOTIFY_ACQUAINTANCES_REPLY 0x00007fe2
#define THORIUM_ACTOR_FORWARD_MESSAGES 0x00000bef
#define THORIUM_ACTOR_FORWARD_MESSAGES_REPLY 0x00002ff3
#define THORIUM_ACTOR_SET_SUPERVISOR 0x0000312f
#define THORIUM_ACTOR_SET_SUPERVISOR_REPLY 0x00007b18

/* name change for acquaintances

Design notes:

in engine/actor.c
change of name

notification_name_change = THORIUM_ACTOR_NOTIFICATION_NAME_CHANGE_NOT_SUPPORTED
THORIUM_ACTOR_NOTIFICATION_NAME_CHANGE_SUPPORTED

if unsupported, auto-reply

THORIUM_ACTOR_NOTIFY_NAME_CHANGE (source is old name, name is new name)

the actor just need to change any acquaintance with old name to
new name.
*/
#define THORIUM_ACTOR_NOTIFY_NAME_CHANGE 0x000068b9
#define THORIUM_ACTOR_NOTIFY_NAME_CHANGE_REPLY 0x00003100

/*
 * Messages for actors that are data stores
 */

/* Auto-scaling stuff
 */

#define THORIUM_ACTOR_ENABLE_AUTO_SCALING 0x00000ede
#define THORIUM_ACTOR_DISABLE_AUTO_SCALING 0x00002b88
#define THORIUM_ACTOR_DO_AUTO_SCALING 0x000064de

/*
 * some actor constants
 */

/* states */
#define THORIUM_ACTOR_STATUS_NOT_SUPPORTED 0
#define THORIUM_ACTOR_STATUS_SUPPORTED 1
#define THORIUM_ACTOR_STATUS_NOT_STARTED 2
#define THORIUM_ACTOR_STATUS_STARTED 3

/* special names */
#define THORIUM_ACTOR_SELF 0
#define THORIUM_ACTOR_SUPERVISOR 1
#define THORIUM_ACTOR_SOURCE 2
#define THORIUM_ACTOR_NOBODY (-1)
#define THORIUM_ACTOR_ANYBODY (-2)
#define THORIUM_ACTOR_SPAWNING_IN_PROGRESS (-3)

#define THORIUM_ACTOR_NO_VALUE -1

/*
 ********************************************
 * In this section are listed some message tags that are available for use
 * by concrete actors.
 *
 */

/*
 * ACTOR_PING can be used by concrete actors, it is
 * not being used by biosal systems.
 */
#define THORIUM_ACTOR_PING 0x000040b3
#define THORIUM_ACTOR_PING_REPLY 0x00006eda

/*
 * The notify messages can be used freely.
 */
#define THORIUM_ACTOR_NOTIFY 0x0000710b
#define THORIUM_ACTOR_NOTIFY_REPLY 0x00005f82

#define THORIUM_ACTOR_RESET 0x00005045
#define THORIUM_ACTOR_RESET_REPLY 0x0000056d

#define THORIUM_ACTOR_BEGIN 0x0000125f
#define THORIUM_ACTOR_BEGIN_REPLY 0x0000214a

/*
 ********************************************
 */

struct thorium_node;
struct thorium_worker;
struct bsal_memory_pool;

/*
 * The mailbox size of an actor.
 * When it is full, messages are buffered upstream.
 */
/*
#define THORIUM_ACTOR_MAILBOX_SIZE 4
*/
#define THORIUM_ACTOR_MAILBOX_SIZE 256

/*
 * The actor attribute is a void *
 */
struct thorium_actor {
    struct thorium_script *script;
    struct thorium_worker *worker;
    struct thorium_node *node;

    int priority;
    uint32_t flags;
    struct bsal_fast_ring mailbox;

    struct bsal_map received_messages;
    struct bsal_map sent_messages;

#ifdef THORIUM_ACTOR_STORE_CHILDREN
    struct bsal_vector acquaintance_vector;
    struct bsal_map acquaintance_map;

    struct bsal_vector children;
    int acquaintance_index;
#endif

    struct bsal_queue enqueued_messages;

    struct thorium_dispatcher dispatcher;
    int current_source;
    void *concrete_actor;

    struct bsal_lock receive_lock;

    /*
     * The name of the actor
     */
    int name;

    /*
     * The name of the supervisor for this
     * actor.
     */
    int supervisor;

    /*
     * The inder of the initial actor to use
     * for spawning new colleagues in the company.
     */
    int spawner_index;

    struct bsal_counter counter;

    int synchronization_responses;
    int synchronization_expected_responses;

    int forwarding_selector;
    struct bsal_queue forwarding_queue;
    struct bsal_queue queued_messages_for_clone;
    struct bsal_queue queued_messages_for_migration;

    int cloning_status;
    int cloning_spawner;
    int cloning_new_actor;
    int cloning_client;

    int migration_status;
    int migration_spawner;
    int migration_new_actor;
    int migration_client;
};

void thorium_actor_init(struct thorium_actor *self, void *state,
                struct thorium_script *script, int name, struct thorium_node *node);
void thorium_actor_destroy(struct thorium_actor *self);

int thorium_actor_name(struct thorium_actor *self);
void *thorium_actor_concrete_actor(struct thorium_actor *self);
void thorium_actor_set_name(struct thorium_actor *self, int name);

void thorium_actor_set_worker(struct thorium_actor *self, struct thorium_worker *worker);
struct thorium_worker *thorium_actor_worker(struct thorium_actor *self);

void thorium_actor_print(struct thorium_actor *self);
int thorium_actor_dead(struct thorium_actor *self);
void thorium_actor_die(struct thorium_actor *self);

thorium_actor_init_fn_t thorium_actor_get_init(struct thorium_actor *self);
thorium_actor_destroy_fn_t thorium_actor_get_destroy(struct thorium_actor *self);
thorium_actor_receive_fn_t thorium_actor_get_receive(struct thorium_actor *self);

/* send functions
 */
void thorium_actor_send(struct thorium_actor *self, int destination, struct thorium_message *message);

void thorium_actor_send_with_source(struct thorium_actor *self, int name, struct thorium_message *message,
                int source);
int thorium_actor_send_system(struct thorium_actor *self, int name, struct thorium_message *message);
int thorium_actor_send_system_self(struct thorium_actor *self, struct thorium_message *message);

int thorium_actor_source(struct thorium_actor *self);

void thorium_actor_receive(struct thorium_actor *self, struct thorium_message *message);
int thorium_actor_receive_system(struct thorium_actor *self, struct thorium_message *message);
int thorium_actor_receive_system_no_pack(struct thorium_actor *self, struct thorium_message *message);

struct thorium_node *thorium_actor_node(struct thorium_actor *self);
int thorium_actor_node_name(struct thorium_actor *self);
int thorium_actor_get_node_count(struct thorium_actor *self);
int thorium_actor_node_worker_count(struct thorium_actor *self);

/*
 * \return This function returns the name of the spawned actor.
 */
int thorium_actor_spawn(struct thorium_actor *self, int script);

void thorium_actor_lock(struct thorium_actor *self);
int thorium_actor_trylock(struct thorium_actor *self);
void thorium_actor_unlock(struct thorium_actor *self);

int thorium_actor_argc(struct thorium_actor *self);
char **thorium_actor_argv(struct thorium_actor *self);

int thorium_actor_supervisor(struct thorium_actor *self);
void thorium_actor_set_supervisor(struct thorium_actor *self, int supervisor);

/* synchronization functions
 */
void thorium_actor_receive_synchronize(struct thorium_actor *self,
                struct thorium_message *message);
void thorium_actor_receive_synchronize_reply(struct thorium_actor *self,
                struct thorium_message *message);
int thorium_actor_synchronization_completed(struct thorium_actor *self);
void thorium_actor_synchronize(struct thorium_actor *self, struct bsal_vector *actors);

void thorium_actor_receive_proxy_message(struct thorium_actor *self,
                struct thorium_message *message);
void thorium_actor_pack_proxy_message(struct thorium_actor *self,
                struct thorium_message *message, int real_source);
int thorium_actor_unpack_proxy_message(struct thorium_actor *self,
                struct thorium_message *message);
void thorium_actor_send_proxy(struct thorium_actor *self, int destination,
                struct thorium_message *message, int real_source);

void thorium_actor_forward_messages(struct thorium_actor *self, struct thorium_message *message);

int thorium_actor_script(struct thorium_actor *self);
struct thorium_script *thorium_actor_get_script(struct thorium_actor *self);
void thorium_actor_add_script(struct thorium_actor *self, int name, struct thorium_script *script);

/* actor cloning */
void thorium_actor_clone(struct thorium_actor *self, struct thorium_message *message);
void thorium_actor_continue_clone(struct thorium_actor *self, struct thorium_message *message);

struct bsal_counter *thorium_actor_counter(struct thorium_actor *self);

/*
 * Functions to use and register handlers
 */
int thorium_actor_use_route(struct thorium_actor *self, struct thorium_message *message);

void thorium_actor_add_route_with_source_and_condition(struct thorium_actor *self, int tag, thorium_actor_receive_fn_t handler,
                int source, int *actual, int expected);

struct thorium_dispatcher *thorium_actor_dispatcher(struct thorium_actor *self);
void thorium_actor_set_node(struct thorium_actor *self, struct thorium_node *node);

void thorium_actor_migrate(struct thorium_actor *self, struct thorium_message *message);

#ifdef THORIUM_ACTOR_STORE_CHILDREN
struct bsal_vector *thorium_actor_acquaintance_vector_private(struct thorium_actor *self);
int thorium_actor_add_acquaintance_private(struct thorium_actor *self, int name);
int thorium_actor_get_acquaintance_private(struct thorium_actor *self, int index);

int thorium_actor_get_acquaintance_index_private(struct thorium_actor *self, int name);
int thorium_actor_acquaintance_count_private(struct thorium_actor *self);
int thorium_actor_add_child(struct thorium_actor *self, int name);
int thorium_actor_get_child(struct thorium_actor *self, int index);
int thorium_actor_get_child_index(struct thorium_actor *self, int name);
int thorium_actor_child_count(struct thorium_actor *self);

void thorium_actor_migrate_notify_acquaintances(struct thorium_actor *self, struct thorium_message *message);
void thorium_actor_notify_name_change(struct thorium_actor *self, struct thorium_message *message);
#endif

void thorium_actor_queue_message(struct thorium_actor *self,
                struct thorium_message *message);
int thorium_actor_spawn_real(struct thorium_actor *self, int script);

void thorium_actor_enqueue_message(struct thorium_actor *self, struct thorium_message *message);
void thorium_actor_dequeue_message(struct thorium_actor *self, struct thorium_message *message);
int thorium_actor_enqueued_message_count(struct thorium_actor *self);

struct bsal_map *thorium_actor_get_received_messages(struct thorium_actor *self);
struct bsal_map *thorium_actor_get_sent_messages(struct thorium_actor *self);

struct bsal_memory_pool *thorium_actor_get_ephemeral_memory(struct thorium_actor *self);
struct thorium_worker *thorium_actor_get_last_worker(struct thorium_actor *self);

int thorium_actor_enqueue_mailbox_message(struct thorium_actor *self, struct thorium_message *message);
int thorium_actor_dequeue_mailbox_message(struct thorium_actor *self, struct thorium_message *message);
int thorium_actor_get_mailbox_size(struct thorium_actor *self);

int thorium_actor_get_sum_of_received_messages(struct thorium_actor *self);
int thorium_actor_work(struct thorium_actor *self);
char *thorium_actor_script_name(struct thorium_actor *self);
void thorium_actor_reset_counters(struct thorium_actor *self);

int thorium_actor_get_priority(struct thorium_actor *self);
void thorium_actor_set_priority(struct thorium_actor *self, int priority);
int thorium_actor_get_source_count(struct thorium_actor *self);

int thorium_actor_get_spawner(struct thorium_actor *self, struct bsal_vector *spawners);

/*
 * Expose the acquaintance API if required.
 */
#ifdef THORIUM_ACTOR_EXPOSE_ACQUAINTANCE_VECTOR

#define thorium_actor_acquaintance_vector thorium_actor_acquaintance_vector_private
#define thorium_actor_add_acquaintance thorium_actor_add_acquaintance_private
#define thorium_actor_get_acquaintance thorium_actor_get_acquaintance_private

#define thorium_actor_get_acquaintance_index thorium_actor_get_acquaintance_index_private
#define thorium_actor_acquaintance_count thorium_actor_acquaintance_count_private

#endif

#endif
