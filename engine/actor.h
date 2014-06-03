
#ifndef _BSAL_ACTOR_H
#define _BSAL_ACTOR_H

#include "message.h"
#include "script.h"

#include <pthread.h>
#include <stdint.h>

/* engine/actor.h */
#define BSAL_TAG_OFFSET_ACTOR 0
#define BSAL_TAG_COUNT_ACTOR 64

/* input/input_actor.h */
#define BSAL_TAG_OFFSET_INPUT_ACTOR ( BSAL_TAG_OFFSET_ACTOR + BSAL_TAG_COUNT_ACTOR )
#define BSAL_TAG_COUNT_INPUT_ACTOR 64

/* the user can start with this value */
#define BSAL_TAG_OFFSET_USER ( BSAL_TAG_OFFSET_INPUT_ACTOR + BSAL_TAG_COUNT_INPUT_ACTOR )

/* counters */

#define BSAL_COUNTER_RECEIVED_MESSAGES 0
#define BSAL_COUNTER_RECEIVED_MESSAGES_SAME_NODE 1
#define BSAL_COUNTER_RECEIVED_MESSAGES_OTHER_NODE 2
#define BSAL_COUNTER_SENT_MESSAGES 3
#define BSAL_COUNTER_SENT_MESSAGES_SAME_NODE 4
#define BSAL_COUNTER_SENT_MESSAGES_OTHER_NODE 5

enum {
    BSAL_ACTOR_START = BSAL_TAG_OFFSET_ACTOR, /* +0 */
    BSAL_ACTOR_BINOMIAL_TREE_SEND,
    BSAL_ACTOR_PROXY_MESSAGE,
    BSAL_ACTOR_PIN,
    BSAL_ACTOR_UNPIN,
    BSAL_ACTOR_SYNCHRONIZE,
    BSAL_ACTOR_SYNCHRONIZE_REPLY,
    BSAL_ACTOR_SYNCHRONIZED,
    BSAL_ACTOR_SPAWN,
    BSAL_ACTOR_SPAWN_REPLY, /* +9 */
    BSAL_ACTOR_STOP /* +10 */
};

struct bsal_node;
struct bsal_worker;

/*
 * the actor attribute is a void *
 */
struct bsal_actor {
    struct bsal_script *script;
    struct bsal_worker *worker;
    struct bsal_worker *affinity_worker;
    void *state;

    pthread_spinlock_t lock;

    int locked;
    int name;
    int dead;
    int supervisor;
    uint64_t counter_received_messages;
    uint64_t counter_sent_messages;

    int synchronization_started;
    int synchronization_responses;
    int synchronization_expected_responses;
};

void bsal_actor_init(struct bsal_actor *actor, void *state,
                struct bsal_script *script);
void bsal_actor_destroy(struct bsal_actor *actor);

int bsal_actor_name(struct bsal_actor *actor);
void *bsal_actor_concrete_actor(struct bsal_actor *actor);
void bsal_actor_set_name(struct bsal_actor *actor, int name);

void bsal_actor_set_worker(struct bsal_actor *actor, struct bsal_worker *worker);
struct bsal_worker *bsal_actor_worker(struct bsal_actor *actor);
struct bsal_worker *bsal_actor_affinity_worker(struct bsal_actor *actor);

void bsal_actor_print(struct bsal_actor *actor);
int bsal_actor_dead(struct bsal_actor *actor);
int bsal_actor_nodes(struct bsal_actor *actor);
void bsal_actor_die(struct bsal_actor *actor);

bsal_actor_init_fn_t bsal_actor_get_init(struct bsal_actor *actor);
bsal_actor_destroy_fn_t bsal_actor_get_destroy(struct bsal_actor *actor);
bsal_actor_receive_fn_t bsal_actor_get_receive(struct bsal_actor *actor);

void bsal_actor_send(struct bsal_actor *actor, int destination, struct bsal_message *message);

void bsal_actor_send_with_source(struct bsal_actor *actor, int name, struct bsal_message *message,
                int source);
int bsal_actor_send_system(struct bsal_actor *actor, int name, struct bsal_message *message);

/* Send a message to a range of actors.
 * The implementation uses a binomial tree.
 */
void bsal_actor_send_range(struct bsal_actor *actor, int first, int last,
                struct bsal_message *message);
void bsal_actor_send_range_standard(struct bsal_actor *actor, int first, int last,
                struct bsal_message *message);
void bsal_actor_send_range_binomial_tree(struct bsal_actor *actor, int first, int last,
                struct bsal_message *message);

void bsal_actor_receive(struct bsal_actor *actor, struct bsal_message *message);
int bsal_actor_receive_system(struct bsal_actor *actor, struct bsal_message *message);
void bsal_actor_receive_binomial_tree_send(struct bsal_actor *actor,
                struct bsal_message *message);

struct bsal_node *bsal_actor_node(struct bsal_actor *actor);

/*
 * \return This function returns the name of the spawned actor.
 */
int bsal_actor_spawn(struct bsal_actor *actor, int script);

void bsal_actor_lock(struct bsal_actor *actor);
void bsal_actor_unlock(struct bsal_actor *actor);

int bsal_actor_workers(struct bsal_actor *actor);
int bsal_actor_threads(struct bsal_actor *actor);
int bsal_actor_argc(struct bsal_actor *actor);
char **bsal_actor_argv(struct bsal_actor *actor);

/* an actor can be pinned to a worker
 * so that the next message is processed
 * on the same worker.
 * this has implications for memory affinity in
 * NUMA systems
 */
void bsal_actor_pin(struct bsal_actor *actor);
void bsal_actor_unpin(struct bsal_actor *actor);

int bsal_actor_supervisor(struct bsal_actor *actor);
void bsal_actor_set_supervisor(struct bsal_actor *actor, int supervisor);

uint64_t bsal_actor_get_counter(struct bsal_actor *actor, int counter);
void bsal_actor_increment_counter(struct bsal_actor *actor, int counter);

/* synchronization functions
 */
void bsal_actor_receive_synchronize(struct bsal_actor *actor,
                struct bsal_message *message);
void bsal_actor_receive_synchronize_reply(struct bsal_actor *actor,
                struct bsal_message *message);
int bsal_actor_synchronization_completed(struct bsal_actor *actor);
void bsal_actor_synchronize(struct bsal_actor *actor, int first_actor, int last_actor);

void bsal_actor_receive_proxy_message(struct bsal_actor *actor,
                struct bsal_message *message);
void bsal_actor_pack_proxy_message(struct bsal_actor *actor,
                int real_source, struct bsal_message *message);
int bsal_actor_unpack_proxy_message(struct bsal_actor *actor,
                struct bsal_message *message);
int bsal_actor_script(struct bsal_actor *actor);
void bsal_actor_add_script(struct bsal_actor *actor, int name, struct bsal_script *script);

#endif
