
#ifndef BSAL_WORKER_POOL_H
#define BSAL_WORKER_POOL_H

#include "worker.h"
#include "work_queue.h"
#include "message_queue.h"

#include <time.h>

struct bsal_node;
struct bsal_worker;

struct bsal_worker_pool {
    struct bsal_work_queue work_queue;
    struct bsal_message_queue message_queue;
    struct bsal_worker *worker_array;
    struct bsal_node *node;

    int worker_for_work;
    int worker_for_message;
    int worker_for_run;

    int workers;

    int ticks_without_messages;

    time_t starting_time;
};

void bsal_worker_pool_init(struct bsal_worker_pool *pool, int workers, struct bsal_node *node);
void bsal_worker_pool_destroy(struct bsal_worker_pool *pool);

void bsal_worker_pool_create_workers(struct bsal_worker_pool *pool);
void bsal_worker_pool_delete_workers(struct bsal_worker_pool *pool);
void bsal_worker_pool_run(struct bsal_worker_pool *pool);

void bsal_worker_pool_start(struct bsal_worker_pool *pool);
void bsal_worker_pool_stop(struct bsal_worker_pool *pool);

int bsal_worker_pool_pull(struct bsal_worker_pool *pool, struct bsal_message *message);

struct bsal_worker *bsal_worker_pool_select_worker_for_run(struct bsal_worker_pool *pool);
struct bsal_worker *bsal_worker_pool_select_worker_for_work(
                struct bsal_worker_pool *node, struct bsal_work *work);
struct bsal_worker *bsal_worker_pool_select_worker_for_message(struct bsal_worker_pool *pool);
int bsal_worker_pool_next_worker(struct bsal_worker_pool *node, int thread);

void bsal_worker_pool_schedule_work(struct bsal_worker_pool *pool, struct bsal_work *work);

int bsal_worker_pool_worker_count(struct bsal_worker_pool *pool);
int bsal_worker_pool_has_messages(struct bsal_worker_pool *pool);

struct bsal_worker *bsal_worker_pool_get_worker(
                struct bsal_worker_pool *self, int index);

struct bsal_worker *bsal_worker_pool_select_worker_round_robin(
                struct bsal_worker_pool *pool, struct bsal_work *work);
struct bsal_worker *bsal_worker_pool_select_worker_least_busy(
                struct bsal_worker_pool *pool, struct bsal_work *work);

void bsal_worker_pool_print_load(struct bsal_worker_pool *self);

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
int bsal_worker_pool_pull_classic(struct bsal_worker_pool *pool, struct bsal_message *message);
void bsal_worker_pool_schedule_work_classic(struct bsal_worker_pool *pool, struct bsal_work *work);

#endif

#endif
