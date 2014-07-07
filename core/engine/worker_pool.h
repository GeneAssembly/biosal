
#ifndef BSAL_WORKER_POOL_H
#define BSAL_WORKER_POOL_H

#include "worker.h"

#include "scheduler.h"

#include <core/structures/ring_queue.h>
#include <core/structures/vector.h>

#include <time.h>

struct bsal_node;
struct bsal_worker;
struct bsal_migration;

/*
#define BSAL_WORKER_POOL_HAS_SPECIAL_QUEUES
*/

struct bsal_worker_pool {

    struct bsal_scheduler scheduler;

    struct bsal_vector worker_actors;

    struct bsal_ring_queue scheduled_actor_queue_buffer;
    struct bsal_ring_queue inbound_message_queue_buffer;

    int workers;
    struct bsal_vector worker_array;
    struct bsal_worker *worker_cache;

    struct bsal_vector message_count_cache;
    int *message_cache;

    struct bsal_node *node;

    int worker_for_message;
    int worker_for_run;
    int debug_mode;

    int last_warning;
    int last_scheduling_warning;

    int received_works;
    int balance_period;

#if 0
    int ticks_without_messages;
#endif

    time_t starting_time;
};

#define BSAL_WORKER_POOL_LOAD_LOOP 0
#define BSAL_WORKER_POOL_LOAD_EPOCH 1

void bsal_worker_pool_init(struct bsal_worker_pool *pool, int workers, struct bsal_node *node);
void bsal_worker_pool_destroy(struct bsal_worker_pool *pool);

void bsal_worker_pool_create_workers(struct bsal_worker_pool *pool);
void bsal_worker_pool_delete_workers(struct bsal_worker_pool *pool);
void bsal_worker_pool_run(struct bsal_worker_pool *pool);

void bsal_worker_pool_start(struct bsal_worker_pool *pool);
void bsal_worker_pool_stop(struct bsal_worker_pool *pool);

struct bsal_worker *bsal_worker_pool_select_worker_for_run(struct bsal_worker_pool *pool);
struct bsal_worker *bsal_worker_pool_select_worker_for_message(struct bsal_worker_pool *pool);
int bsal_worker_pool_next_worker(struct bsal_worker_pool *node, int thread);

int bsal_worker_pool_worker_count(struct bsal_worker_pool *pool);

#if 0
int bsal_worker_pool_has_messages(struct bsal_worker_pool *pool);
#endif

struct bsal_worker *bsal_worker_pool_get_worker(
                struct bsal_worker_pool *self, int index);

void bsal_worker_pool_print_load(struct bsal_worker_pool *self, int type);

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
int bsal_worker_pool_pull_classic(struct bsal_worker_pool *pool, struct bsal_message *message);
void bsal_worker_pool_schedule_work_classic(struct bsal_worker_pool *pool, struct bsal_work *work);

#endif

void bsal_worker_pool_toggle_debug_mode(struct bsal_worker_pool *self);
void bsal_worker_pool_set_cached_value(struct bsal_worker_pool *self, int index, int value);
int bsal_worker_pool_get_cached_value(struct bsal_worker_pool *self, int index);

int bsal_worker_pool_enqueue_message(struct bsal_worker_pool *pool, struct bsal_message *message);
int bsal_worker_pool_dequeue_message(struct bsal_worker_pool *pool, struct bsal_message *message);

void bsal_worker_pool_print_efficiency(struct bsal_worker_pool *pool);

struct bsal_node *bsal_worker_pool_get_node(struct bsal_worker_pool *pool);
void bsal_worker_pool_give_message_to_actor(struct bsal_worker_pool *pool, struct bsal_message *message);

#endif
