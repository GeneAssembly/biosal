
#include "worker_pool.h"

#include "actor.h"
#include "work.h"
#include "worker.h"
#include "node.h"

#include <core/helpers/vector_helper.h>
#include <core/system/memory.h>

#include <stdlib.h>
#include <stdio.h>

#define BSAL_WORKER_POOL_USE_LEAST_BUSY
#define BSAL_WORKER_POOL_WORK_SCHEDULING_WINDOW 4
#define BSAL_WORKER_POOL_MESSAGE_SCHEDULING_WINDOW 4

#define BSAL_WORKER_POOL_DEBUG_ISSUE_334

/*
#define BSAL_WORKER_POOL_DEBUG
*/
/*
*/
#define BSAL_WORKER_POOL_PUSH_WORK_ON_SAME_WORKER

void bsal_worker_pool_init(struct bsal_worker_pool *pool, int workers,
                struct bsal_node *node)
{
    pool->debug_mode = 0;
    pool->node = node;

#if 0
    pool->ticks_without_messages = 0;
#endif

    pool->last_warning = 0;
    pool->last_scheduling_warning = 0;

    pool->workers = workers;

    /* with only one thread,  the main thread
     * handles everything.
     */
    if (pool->workers >= 1) {
        pool->worker_for_run = 0;
        pool->worker_for_message = 0;
        pool->worker_for_work = 0;
    } else {
        printf("Error: the number of workers must be at least 1.\n");
        exit(1);
    }

#ifdef BSAL_WORKER_POOL_HAS_SPECIAL_QUEUES
    bsal_work_queue_init(&pool->work_queue);
    bsal_message_queue_init(&pool->message_queue);
#endif

    bsal_worker_pool_create_workers(pool);

    pool->starting_time = time(NULL);

    bsal_ring_queue_init(&pool->local_work_queue, sizeof(struct bsal_work));
}

void bsal_worker_pool_destroy(struct bsal_worker_pool *pool)
{
    bsal_worker_pool_delete_workers(pool);

    pool->node = NULL;

#ifdef BSAL_WORKER_POOL_HAS_SPECIAL_QUEUES
    bsal_work_queue_destroy(&pool->work_queue);
    bsal_message_queue_destroy(&pool->message_queue);
#endif

    bsal_ring_queue_destroy(&pool->local_work_queue);
}

void bsal_worker_pool_delete_workers(struct bsal_worker_pool *pool)
{
    int i = 0;
    struct bsal_worker *worker;

    if (pool->workers <= 0) {
        return;
    }

    for (i = 0; i < pool->workers; i++) {
        worker = bsal_worker_pool_get_worker(pool, i);

#if 0
        printf("worker/%d loop_load %f\n", bsal_worker_name(worker),
                    bsal_worker_get_loop_load(worker));
#endif

        bsal_worker_destroy(worker);
    }

    bsal_vector_destroy(&pool->worker_array);
    bsal_vector_destroy(&pool->message_count_cache);
}

void bsal_worker_pool_create_workers(struct bsal_worker_pool *pool)
{
    int i;

    if (pool->workers <= 0) {
        return;
    }

    bsal_vector_init(&pool->worker_array, sizeof(struct bsal_worker));
    bsal_vector_init(&pool->message_count_cache, sizeof(int));

    bsal_vector_resize(&pool->worker_array, pool->workers);
    bsal_vector_resize(&pool->message_count_cache, pool->workers);

    pool->worker_cache = (struct bsal_worker *)bsal_vector_at(&pool->worker_array, 0);
    pool->message_cache = (int *)bsal_vector_at(&pool->message_count_cache, 0);

    for (i = 0; i < pool->workers; i++) {
        bsal_worker_init(bsal_worker_pool_get_worker(pool, i), i, pool->node);
        bsal_vector_helper_set_int(&pool->message_count_cache, i, 0);
    }
}

void bsal_worker_pool_start(struct bsal_worker_pool *pool)
{
    int i;
    int processor;

    /* start workers
     *
     * we start at 1 because the thread 0 is
     * used by the main thread...
     */
    for (i = 0; i < pool->workers; i++) {
        processor = i;

        if (bsal_node_nodes(pool->node) != 1) {
            processor = -1;
        }

        bsal_worker_start(bsal_worker_pool_get_worker(pool, i), processor);
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

#if 0
    if (!answer) {
        pool->ticks_without_messages++;
    } else {
        pool->ticks_without_messages = 0;
    }
#endif

    return answer;
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
int bsal_worker_pool_pull_classic(struct bsal_worker_pool *pool, struct bsal_message *message)
{
    struct bsal_worker *worker;
    int answer;

    worker = bsal_worker_pool_select_worker_for_message(pool);
    answer = bsal_worker_pull_message(worker, message);

    return answer;
}

/* select a worker to pull from */
struct bsal_worker *bsal_worker_pool_select_worker_for_message(struct bsal_worker_pool *pool)
{
    int index;
    int i;
    int score;
    struct bsal_worker *worker;
    int attempts;
    struct bsal_worker *best_worker;
    int best_score;
    int best_index;

    best_index = -1;
    best_score = 0;
    best_worker = NULL;

    i = 0;
    attempts = BSAL_WORKER_POOL_MESSAGE_SCHEDULING_WINDOW;

    /* select thet worker with the most messages in the window.
     */
    while (i < attempts) {

        index = pool->worker_for_message;
        pool->worker_for_message = bsal_worker_pool_next_worker(pool, index);
        worker = bsal_worker_pool_get_worker(pool, index);
        score = bsal_worker_pool_get_cached_value(pool, index);

        /* Update the cache.
         * This is expensive because it will touch the cache line.
         * Only the worker is increasing the number of messages, and
         * only the worker pool is decreasing it.
         * As long as the cached value is greater than 0, then there is
         * definitely something to pull without the need
         * to break the CPU cache line
         *
         */
        /* always update cache because otherwise there will be
         * starvation
         */
        if (1 || score == 0) {
            score = bsal_worker_get_message_production_score(worker);
            bsal_worker_pool_set_cached_value(pool, index, score);
        }

        if (best_worker == NULL || score > best_score) {
            best_worker = worker;
            best_score = score;
            best_index = index;
        }

        ++i;
    }

    /* Update the cached value for the winning worker to have an
     * accurate value for this worker.
     */
    bsal_vector_helper_set_int(&pool->message_count_cache, best_index, best_score - 1);

    return best_worker;
}

int bsal_worker_pool_next_worker(struct bsal_worker_pool *pool, int worker)
{
    worker++;

    /* wrap the counter
     */
    if (worker == pool->workers) {
        worker = 0;
    }

    return worker;
}

/* select the worker to push work to */
struct bsal_worker *bsal_worker_pool_select_worker_for_work(
                struct bsal_worker_pool *pool, struct bsal_work *work)
{
#ifdef BSAL_WORKER_POOL_PUSH_WORK_ON_SAME_WORKER

/*#error "BAD"*/
    /* check first if the actor is active.
     * If it is active, just enqueue the work at the same
     * place (on the same worker).
     * This avoids contention.
     * This is only required for important actors...
     * Important actors are those who receive a lot of messages.
     * This is roughly equivalent to have a dedicated worker for
     * an important worker.
     */
    struct bsal_actor *actor;
    struct bsal_worker *current_worker;
    int best_score;
    struct bsal_worker *last_worker;

    actor = bsal_work_actor(work);
    current_worker = bsal_actor_worker(actor);

    if (current_worker != NULL) {
#if 0
        printf("USING current worker\n");
#endif

#ifdef BSAL_WORKER_POOL_DEBUG_SCHEDULING_SAME_WORKER
        if (bsal_actor_script(actor) == (int)0x82673850) {
            printf("DEBUG7890 node %d scheduling work for aggregator on worker %d\n",
                            bsal_node_name(pool->node),
                            bsal_worker_name(current_worker));
        }
#endif

        return current_worker;
    }
#endif

#ifdef BSAL_WORKER_POOL_USE_LEAST_BUSY
    current_worker = bsal_worker_pool_select_worker_least_busy(pool, work, &best_score);

    /* if the best score is not 0, try the last worker
     */
    if (best_score != 0) {
        actor = bsal_work_actor(work);
        last_worker = bsal_actor_get_last_worker(actor);

        if (last_worker != NULL) {

            return last_worker;
/*
            last_worker_score = bsal_worker_get_work_scheduling_score(last_worker);
        */
        }
    }

    /* return the worker with the lowest load
     */
    return current_worker;

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
    return self->worker_cache + index;
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
struct bsal_worker *bsal_worker_pool_select_worker_least_busy(
                struct bsal_worker_pool *self, struct bsal_work *work, int *worker_score)
{
    int to_check;
    int score;
    int best_score;
    struct bsal_worker *worker;
    struct bsal_worker *best_worker;

#if 0
    int last_worker_score;
#endif

#ifdef BSAL_WORKER_DEBUG
    int tag;
    int destination;
    struct bsal_message *message;
#endif

    best_worker = NULL;
    best_score = 99;

    to_check = BSAL_WORKER_POOL_WORK_SCHEDULING_WINDOW;

    while (to_check--) {

        /*
         * get the worker to test for this iteration.
         */
        worker = bsal_worker_pool_get_worker(self, self->worker_for_work);

        score = bsal_worker_get_work_scheduling_score(worker);

#ifdef BSAL_WORKER_POOL_DEBUG_ISSUE_334
        if (score >= BSAL_WORKER_WARNING_THRESHOLD
                        && (self->last_scheduling_warning == 0
                             || score >= self->last_scheduling_warning + BSAL_WORKER_WARNING_THRESHOLD_STRIDE)) {
            printf("Warning: node %d worker %d has a scheduling score of %d\n",
                            bsal_node_name(self->node),
                            self->worker_for_work, score);

            self->last_scheduling_warning = score;
        }
#endif

        /* if the worker is not busy and it has no work to do,
         * select it right away...
         */
        if (score == 0) {
            best_worker = worker;
            best_score = 0;
            break;
        }

        /* Otherwise, test the worker
         */
        if (best_worker == NULL || score < best_score) {
            best_worker = worker;
            best_score = score;
        }

        /*
         * assign the next worker
         */
        self->worker_for_work = bsal_worker_pool_next_worker(self, self->worker_for_work);
    }

#ifdef BSAL_WORKER_POOL_DEBUG
    message = bsal_work_message(work);
    tag = bsal_message_tag(message);
    destination = bsal_message_destination(message);

    if (tag == BSAL_ACTOR_ASK_TO_STOP) {
        printf("DEBUG dispatching BSAL_ACTOR_ASK_TO_STOP for actor %d to worker %d\n",
                        destination, *start);
    }


#endif

    /*
     * assign the next worker
     */
    self->worker_for_work = bsal_worker_pool_next_worker(self, self->worker_for_work);

    *worker_score = best_score;
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

void bsal_worker_pool_schedule_work(struct bsal_worker_pool *pool, struct bsal_work *work)
{
    struct bsal_work other_work;

#ifdef BSAL_WORKER_POOL_DEBUG
    if (pool->debug_mode) {
        printf("DEBUG bsal_worker_pool_schedule_work called\n");
    }

    int tag;
    int destination;
    struct bsal_message *message;

    message = bsal_work_message(work);
    tag = bsal_message_tag(message);
    destination = bsal_message_destination(message);

    if (tag == BSAL_ACTOR_ASK_TO_STOP) {
        printf("DEBUG bsal_worker_pool_schedule_work tag BSAL_ACTOR_ASK_TO_STOP actor %d\n",
                        destination);
    }
#endif

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
    bsal_worker_pool_schedule_work_classic(pool, work);

    if (bsal_ring_queue_dequeue(&pool->local_work_queue, &other_work)) {
        bsal_worker_pool_schedule_work_classic(pool, &other_work);
    }
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
void bsal_worker_pool_schedule_work_classic(struct bsal_worker_pool *pool, struct bsal_work *work)
{
    struct bsal_worker *worker;
    int count;

    worker = bsal_worker_pool_select_worker_for_work(pool, work);

    /* if the work can not be queued,
     * it will be queued later
     */
    if (!bsal_worker_push_work(worker, work)) {
        bsal_ring_queue_enqueue(&pool->local_work_queue, work);

        count = bsal_ring_queue_size(&pool->local_work_queue);

        if (count >= BSAL_WORKER_WARNING_THRESHOLD
                        && (pool->last_warning == 0
                                || count >= pool->last_warning + BSAL_WORKER_WARNING_THRESHOLD_STRIDE)) {
            printf("Warning node %d has %d works in its local queue\n",
                            bsal_node_name(pool->node), count);

            pool->last_warning = count;
        }
    }
}
#endif

int bsal_worker_pool_worker_count(struct bsal_worker_pool *pool)
{
    return pool->workers;
}

#if 0
int bsal_worker_pool_has_messages(struct bsal_worker_pool *pool)
{
    int threshold;

    threshold = 200000;

    if (pool->ticks_without_messages > threshold) {
        return 0;
    }

    return 1;
}
#endif

void bsal_worker_pool_print_load(struct bsal_worker_pool *self, int type)
{
    int count;
    int i;
    float epoch_load;
    struct bsal_worker *worker;
    float loop_load;
    /*
    int scheduling_score;
    */
    int node_name;
    char *buffer;
    int allocated;
    int offset;
    int extra;
    clock_t current_time;
    int elapsed;
    float selected_load;
    char loop[] = "LOOP";
    char epoch[] = "EPOCH";
    char *description;

    description = NULL;

    if (type == BSAL_WORKER_POOL_LOAD_LOOP) {
        description = loop;
    } else if (type == BSAL_WORKER_POOL_LOAD_EPOCH) {
        description = epoch;
    } else {
        return;
    }

    current_time = time(NULL);
    elapsed = current_time - self->starting_time;

    extra = 100;

    count = bsal_worker_pool_worker_count(self);
    allocated = count * 20 + 20 + extra;

    buffer = bsal_memory_allocate(allocated);
    node_name = bsal_node_name(self->node);
    offset = 0;
    i = 0;

    while (i < count && offset + extra < allocated) {

        worker = bsal_worker_pool_get_worker(self, i);
        epoch_load = bsal_worker_get_epoch_load(worker);
        loop_load = bsal_worker_get_loop_load(worker);
        /*scheduling_score = bsal_worker_get_scheduling_score(worker);*/

        selected_load = epoch_load;

        if (type == BSAL_WORKER_POOL_LOAD_EPOCH) {
            selected_load = epoch_load;
        } else if (type == BSAL_WORKER_POOL_LOAD_LOOP) {
            selected_load = loop_load;
        }
        offset += sprintf(buffer + offset, " %.2f", selected_load);

        ++i;
    }

    printf("LOAD %s %d s node/%d%s\n", description, elapsed, node_name, buffer);

    bsal_memory_free(buffer);
}

void bsal_worker_pool_toggle_debug_mode(struct bsal_worker_pool *self)
{
    self->debug_mode = !self->debug_mode;
}

int bsal_worker_pool_get_cached_value(struct bsal_worker_pool *self, int index)
{
    return self->message_cache[index];
}

void bsal_worker_pool_set_cached_value(struct bsal_worker_pool *self, int index, int value)
{
    self->message_cache[index] = value;
}
