
#include "worker_pool.h"

#include "actor.h"
#include "worker.h"
#include "node.h"
#include "migration.h"

#include <core/helpers/vector_helper.h>
#include <core/helpers/statistics.h>
#include <core/helpers/pair.h>

#include <core/structures/set_iterator.h>
#include <core/structures/vector_iterator.h>

#include <core/system/memory.h>

#include <stdlib.h>
#include <stdio.h>


/*
#define BSAL_WORKER_POOL_DEBUG
#define BSAL_WORKER_POOL_DEBUG_ISSUE_334
#define BSAL_WORKER_POOL_USE_CURRENT_WORKER
*/

/*
 * Scheduling options.
 */
#define BSAL_WORKER_POOL_PUSH_WORK_ON_SAME_WORKER
#define BSAL_WORKER_POOL_FORCE_LAST_WORKER 1
#define BSAL_WORKER_POOL_USE_LEAST_BUSY

/*
*/
#define BSAL_WORKER_POOL_BALANCE

void bsal_worker_pool_init(struct bsal_worker_pool *pool, int workers,
                struct bsal_node *node)
{
    int i;
    struct bsal_set *set;

    pool->debug_mode = 0;
    pool->node = node;

    bsal_scheduler_init(&pool->scheduler, pool);

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

    bsal_ring_queue_init(&pool->scheduled_actor_queue_buffer, sizeof(struct bsal_actor *));
    bsal_ring_queue_init(&pool->inbound_message_queue_buffer, sizeof(struct bsal_message));

    bsal_vector_init(&pool->worker_actors, sizeof(struct bsal_set));

    bsal_vector_resize(&pool->worker_actors, pool->workers);

    for (i = 0; i < pool->workers; i++) {

        set = (struct bsal_set *)bsal_vector_at(&pool->worker_actors, i);

        bsal_set_init(set, sizeof(int));
    }

    pool->received_works = 0;

    pool->balance_period = pool->workers * BSAL_SCHEDULER_REDUCTIONS_PER_WORKER;
}

void bsal_worker_pool_destroy(struct bsal_worker_pool *pool)
{
    int i;
    struct bsal_set *set;

    bsal_scheduler_destroy(&pool->scheduler);

    bsal_worker_pool_print_efficiency(pool);

    for (i = 0; i < pool->workers; i++) {
        set= (struct bsal_set *)bsal_vector_at(&pool->worker_actors, i);

        bsal_set_destroy(set);
    }

    bsal_worker_pool_delete_workers(pool);

    pool->node = NULL;

    bsal_ring_queue_destroy(&pool->inbound_message_queue_buffer);
    bsal_ring_queue_destroy(&pool->scheduled_actor_queue_buffer);

    bsal_vector_destroy(&pool->worker_actors);
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

struct bsal_worker *bsal_worker_pool_select_worker_for_run(struct bsal_worker_pool *pool)
{
    int index;

    index = pool->worker_for_run;
    return bsal_worker_pool_get_worker(pool, index);
}

/* All messages go through here.
 */
int bsal_worker_pool_enqueue_message(struct bsal_worker_pool *pool, struct bsal_message *message)
{
    struct bsal_message other_message;
    struct bsal_actor *actor;
    int worker_index;
    struct bsal_worker *worker;
    int name;
    int destination;

    destination = bsal_message_destination(message);

#if 0
    printf("DEBUG pool receives message for actor %d\n",
                    destination);
#endif

#ifdef BSAL_WORKER_POOL_BALANCE
    /* balance the pool regularly
     */
    if (pool->received_works % pool->balance_period == 0) {
        bsal_scheduler_balance(&pool->scheduler);
    }
#endif

    pool->received_works++;

    name = destination;
    actor = bsal_node_get_actor_from_name(pool->node, name);

    bsal_worker_pool_give_message_to_actor(pool, message);

    /* If there are messages in the inbound message buffer,
     * Try to give  them too.
     */
    if (bsal_ring_queue_dequeue(&pool->inbound_message_queue_buffer, &other_message)) {
        bsal_worker_pool_give_message_to_actor(pool, &other_message);
    }

    /* Try to dequeue an actor for scheduling
     */

    if (bsal_ring_queue_dequeue(&pool->scheduled_actor_queue_buffer, &actor)) {

        name = bsal_actor_name(actor);
        worker_index = bsal_scheduler_get_actor_worker(&pool->scheduler, name);

        worker = bsal_worker_pool_get_worker(pool, worker_index);

        if (!bsal_worker_enqueue_actor(worker, &actor)) {
            bsal_ring_queue_enqueue(&pool->scheduled_actor_queue_buffer, &actor);
        }
    }

    return 1;
}

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
    float sum;
    char loop[] = "LOOP";
    char epoch[] = "EPOCH";
    char *description;
    float efficiency;

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
    sum = 0;

    while (i < count && offset + extra < allocated) {

        worker = bsal_worker_pool_get_worker(self, i);
        epoch_load = bsal_worker_get_epoch_load(worker);
        loop_load = bsal_worker_get_loop_load(worker);

        selected_load = epoch_load;

        if (type == BSAL_WORKER_POOL_LOAD_EPOCH) {
            selected_load = epoch_load;
        } else if (type == BSAL_WORKER_POOL_LOAD_LOOP) {
            selected_load = loop_load;
        }

        /*
        offset += sprintf(buffer + offset, " [%d %d %.2f]", i,
                        scheduling_score,
                        selected_load);
                        */
        offset += sprintf(buffer + offset, " %.2f",
                        selected_load);

        sum += epoch_load;

        ++i;
    }

    efficiency = sum / count;

    printf("LOAD %s %d s node/%d %.2f/%d (%.2f)%s\n",
                    description, elapsed, node_name,
                    sum, count, efficiency, buffer);


    bsal_memory_free(buffer);
}

void bsal_worker_pool_toggle_debug_mode(struct bsal_worker_pool *self)
{
    self->debug_mode = !self->debug_mode;
}

void bsal_worker_pool_print_efficiency(struct bsal_worker_pool *pool)
{
    double efficiency;
    struct bsal_worker *worker;
    int i;

    efficiency = 0;

    for (i = 0; i < pool->workers; i++) {
        worker = bsal_worker_pool_get_worker(pool, i);
        efficiency += bsal_worker_get_loop_load(worker);
    }

    efficiency /= pool->workers;

    printf("node %d efficiency: %.2f\n",
                    bsal_node_name(pool->node),
                    efficiency);

}

struct bsal_node *bsal_worker_pool_get_node(struct bsal_worker_pool *pool)
{
    return pool->node;
}


void bsal_worker_pool_give_message_to_actor(struct bsal_worker_pool *pool, struct bsal_message *message)
{
    int destination;
    struct bsal_actor *actor;
    struct bsal_worker *affinity_worker;
    int worker_index;
    int score;
    int name;
    struct bsal_set *set;

    destination = bsal_message_destination(message);
    actor = bsal_node_get_actor_from_name(pool->node, destination);

#if 0
    printf("DEBUG bsal_worker_pool_give_message_to_actor %d\n", destination);
#endif

    if (actor == NULL) {
        printf("DEAD LETTER CHANNEL...\n");
        return;
    }

    name = bsal_actor_name(actor);

    /* give the message to the actor
     */
    if (!bsal_actor_enqueue_mailbox_message(actor, message)) {
        bsal_ring_queue_enqueue(&pool->inbound_message_queue_buffer, message);

    /* Check if the actor is assigned to a worker
     */
    } else {
/*
        printf("DEBUG message was enqueued in actor mailbox\n");
        */

        worker_index = bsal_scheduler_get_actor_worker(&pool->scheduler, name);
        if (worker_index >= 0) {

            affinity_worker = bsal_worker_pool_get_worker(pool, worker_index);

            /*
            printf("DEBUG actor has an assigned worker\n");
            */

            if (!bsal_worker_enqueue_actor(affinity_worker, &actor)) {
                bsal_ring_queue_enqueue(&pool->scheduled_actor_queue_buffer, &actor);
            }

        } else {

                /*
            printf("DEBUG Needs to do actor placement\n");
            */
            /* assign this actor to the least busy actor
             */
            worker_index = bsal_scheduler_select_worker_least_busy(&pool->scheduler, message, &score);

            bsal_scheduler_set_actor_worker(&pool->scheduler, name, worker_index);
            set = (struct bsal_set *)bsal_vector_at(&pool->worker_actors, worker_index);
            bsal_set_add(set, &name);

            affinity_worker = bsal_worker_pool_get_worker(pool, worker_index);

            if (!bsal_worker_enqueue_actor(affinity_worker, &actor)) {
                bsal_ring_queue_enqueue(&pool->scheduled_actor_queue_buffer, &actor);
            }
        }
    }
}


