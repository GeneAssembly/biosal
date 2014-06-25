
#include "worker_pool.h"

#include "actor.h"
#include "work.h"
#include "worker.h"
#include "node.h"

#include <system/memory.h>

#include <stdlib.h>
#include <stdio.h>

#define BSAL_WORKER_POOL_USE_LEAST_BUSY
#define BSAL_WORKER_POOL_ATTEMPT_COUNT 4

void bsal_worker_pool_init(struct bsal_worker_pool *pool, int workers,
                struct bsal_node *node)
{
    pool->node = node;
    pool->workers = workers;
    pool->worker_array = NULL;
    pool->ticks_without_messages = 0;

    /* with only one thread,  the main thread
     * handles everything.
     */
    if (pool->workers >= 1) {
        pool->worker_for_run = 0;
        pool->worker_for_message = 0;
    } else {
        printf("Error: the number of workers must be at least 1.\n");
        exit(1);
    }

    bsal_work_queue_init(&pool->work_queue);
    bsal_message_queue_init(&pool->message_queue);
    bsal_worker_pool_create_workers(pool);

    pool->starting_time = time(NULL);
}

void bsal_worker_pool_destroy(struct bsal_worker_pool *pool)
{
    bsal_worker_pool_delete_workers(pool);

    pool->node = NULL;
    bsal_work_queue_destroy(&pool->work_queue);
    bsal_message_queue_destroy(&pool->message_queue);
}

void bsal_worker_pool_delete_workers(struct bsal_worker_pool *pool)
{
    int i = 0;

    if (pool->workers <= 0) {
        return;
    }

    for (i = 0; i < pool->workers; i++) {
        bsal_worker_destroy(bsal_worker_pool_get_worker(pool, i));
    }

    bsal_free(pool->worker_array);
    pool->worker_array = NULL;
}

void bsal_worker_pool_create_workers(struct bsal_worker_pool *pool)
{
    int bytes;
    int i;

    if (pool->workers <= 0) {
        return;
    }

    bytes = pool->workers * sizeof(struct bsal_worker);
    pool->worker_array = (struct bsal_worker *)bsal_malloc(bytes);

    for (i = 0; i < pool->workers; i++) {
        bsal_worker_init(bsal_worker_pool_get_worker(pool, i), i, pool->node,
                        &pool->work_queue, &pool->message_queue);
    }
}

void bsal_worker_pool_start(struct bsal_worker_pool *pool)
{
    int i;

    /* start workers
     *
     * we start at 1 because the thread 0 is
     * used by the main thread...
     */
    for (i = 0; i < pool->workers; i++) {
        bsal_worker_start(bsal_worker_pool_get_worker(pool, i));
    }
}

void bsal_worker_pool_run(struct bsal_worker_pool *pool)
{
    /* make the thread work (this is the main thread) */
    bsal_worker_run(bsal_worker_pool_select_worker_for_run(pool));
}

void bsal_worker_pool_stop(struct bsal_worker_pool *pool)
{
    int i;
    /*
     * stop workers
     */

#ifdef BSAL_WORKER_POOL_DEBUG
    printf("Stop workers\n");
#endif

    for (i = 0; i < pool->workers; i++) {
        bsal_worker_stop(bsal_worker_pool_get_worker(pool, i));
    }
}

int bsal_worker_pool_pull(struct bsal_worker_pool *pool, struct bsal_message *message)
{
    int answer;

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
    answer = bsal_worker_pool_pull_classic(pool, message);
#else
    answer = bsal_message_queue_dequeue(&pool->message_queue, message);
#endif

    if (!answer) {
        pool->ticks_without_messages++;
    } else {
        pool->ticks_without_messages = 0;
    }

    return answer;
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
int bsal_worker_pool_pull_classic(struct bsal_worker_pool *pool, struct bsal_message *message)
{
    struct bsal_worker *worker;
    int answer;
    int attempts;
    int i;

    i = 0;
    answer = 0;
    attempts = BSAL_WORKER_POOL_ATTEMPT_COUNT;

    while (answer == 0 && i < attempts) {
        worker = bsal_worker_pool_select_worker_for_message(pool);
        answer = bsal_worker_pull_message(worker, message);
        i++;
    }

    return answer;
}

/* select a worker to pull from */
struct bsal_worker *bsal_worker_pool_select_worker_for_message(struct bsal_worker_pool *pool)
{
    int index;

    index = pool->worker_for_message;
    pool->worker_for_message = bsal_worker_pool_next_worker(pool, index);
    return bsal_worker_pool_get_worker(pool, index);
}

int bsal_worker_pool_next_worker(struct bsal_worker_pool *pool, int worker)
{
    worker++;
    worker %= pool->workers;

    return worker;
}

/* select the worker to push work to */
struct bsal_worker *bsal_worker_pool_select_worker_for_work(
                struct bsal_worker_pool *pool, struct bsal_work *work,
                int *start)
{
#ifdef BSAL_WORKER_POOL_USE_LEAST_BUSY
    return bsal_worker_pool_select_worker_least_busy(pool, work, start);

#else
    return bsal_worker_pool_select_worker_round_robin(pool, work);
#endif
}

struct bsal_worker *bsal_worker_pool_select_worker_round_robin(
                struct bsal_worker_pool *pool, struct bsal_work *work)
{
    int index;
    struct bsal_worker *worker;

    /* check if actor has an affinity worker */
    worker = bsal_actor_affinity_worker(bsal_work_actor(work));

    if (worker != NULL) {
        return worker;
    }

    /* otherwise, pick a worker with round robin */
    index = pool->worker_for_message;
    pool->worker_for_message = bsal_worker_pool_next_worker(pool, pool->worker_for_message);

    return bsal_worker_pool_get_worker(pool, index);
}
#endif

struct bsal_worker *bsal_worker_pool_get_worker(
                struct bsal_worker_pool *self, int index)
{
    if (index < 0 || index >= self->workers) {
        return NULL;
    }

    return self->worker_array + index;
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
struct bsal_worker *bsal_worker_pool_select_worker_least_busy(
                struct bsal_worker_pool *self, struct bsal_work *work,
                int *start)
{
    int to_check;
    int score;
    int best_score;
    struct bsal_worker *worker;
    struct bsal_worker *best_worker;

    best_worker = NULL;
    best_score = 99;

    to_check = BSAL_WORKER_POOL_ATTEMPT_COUNT;

    while (to_check--) {

        /*
         * get the worker to test for this iteration.
         */
        worker = bsal_worker_pool_get_worker(self, *start);

        /*
         * assign the next worker
         */
        *start = bsal_worker_pool_next_worker(self, *start);

        score = bsal_worker_get_scheduling_score(worker);

        /* if the worker is not busy and it has no work to do,
         * select it right away...
         */
        if (score == 0) {
            return worker;
        }

        /* Otherwise, test the worker
         */
        if (best_worker == NULL || score < best_score) {
            best_worker = worker;
            best_score = score;
        }
    }

    /* This is a best effort algorithm
     */
    return best_worker;
}

#endif

struct bsal_worker *bsal_worker_pool_select_worker_for_run(struct bsal_worker_pool *pool)
{
    int index;

    index = pool->worker_for_run;
    return bsal_worker_pool_get_worker(pool, index);
}

void bsal_worker_pool_schedule_work(struct bsal_worker_pool *pool, struct bsal_work *work,
                int *start)
{
#ifdef BSAL_WORKER_HAS_OWN_QUEUES
    bsal_worker_pool_schedule_work_classic(pool, work, start);
#else
    bsal_work_queue_enqueue(&pool->work_queue, work);
#endif
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
/*
 * names are based on names found in:
 * \see http://lxr.free-electrons.com/source/include/linux/workqueue.h
 * \see http://lxr.free-electrons.com/source/kernel/workqueue.c
 */
void bsal_worker_pool_schedule_work_classic(struct bsal_worker_pool *pool, struct bsal_work *work,
                int *start)
{
    struct bsal_worker *worker;

    worker = bsal_worker_pool_select_worker_for_work(pool, work, start);

    /* bsal_worker_push_message use a lock */
    bsal_worker_push_work(worker, work);
}
#endif

int bsal_worker_pool_worker_count(struct bsal_worker_pool *pool)
{
    return pool->workers;
}

int bsal_worker_pool_has_messages(struct bsal_worker_pool *pool)
{
    int threshold;

    threshold = 200000;

    if (pool->ticks_without_messages > threshold) {
        return 0;
    }

    return 1;
}

void bsal_worker_pool_print_load(struct bsal_worker_pool *self)
{
    int count;
    int i;
    float epoch_load;
    struct bsal_worker *worker;
    /*
    float loop_load;
    int scheduling_score;
    */
    int node_name;
    char *buffer;
    int allocated;
    int offset;
    int extra;
    clock_t current_time;
    int elapsed;

    current_time = time(NULL);
    elapsed = current_time - self->starting_time;

    extra = 100;

    count = bsal_worker_pool_worker_count(self);
    allocated = count * 20 + 20 + extra;

    buffer = bsal_malloc(allocated);
    node_name = bsal_node_name(self->node);
    offset = 0;
    i = 0;

    while (i < count && offset + extra < allocated) {

        worker = bsal_worker_pool_get_worker(self, i);
        epoch_load = bsal_worker_get_epoch_load(worker);
        /*
        loop_load = bsal_worker_get_loop_load(worker);
        scheduling_score = bsal_worker_get_scheduling_score(worker);
        */

        offset += sprintf(buffer + offset, " %.2f", epoch_load);
        i++;
    }

    printf("LOAD %d s node/%d%s\n", elapsed, node_name, buffer);

    bsal_free(buffer);
}
