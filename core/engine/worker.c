
#include "worker.h"

#include "message.h"
#include "node.h"
#include "scheduler.h"

#include <core/structures/map.h>
#include <core/structures/vector.h>
#include <core/structures/vector_iterator.h>
#include <core/structures/map_iterator.h>
#include <core/structures/set_iterator.h>

#include <core/helpers/vector_helper.h>
#include <core/system/memory.h>
#include <core/system/timer.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*#define BSAL_WORKER_DEBUG
  */

#define STATUS_IDLE 0
#define STATUS_QUEUED 1

/*
#define BSAL_WORKER_DEBUG_MEMORY
*/

#define BSAL_WORKER_UNPRODUCTIVE_TICK_LIMIT 256

void bsal_worker_init(struct bsal_worker *worker, int name, struct bsal_node *node)
{
    int capacity;
    int ephemeral_memory_block_size;

    capacity = BSAL_WORKER_RING_CAPACITY;
    /*worker->work_queue = work_queue;*/
    worker->node = node;
    worker->name = name;
    worker->dead = 0;
    worker->last_warning = 0;

    /*worker->work_queue = &worker->works;*/

    /* There are two options:
     * 1. enable atomic operations for change visibility
     * 2. Use volatile head and tail.
     */
    bsal_fast_ring_init(&worker->actors_to_schedule, capacity, sizeof(struct bsal_actor *));

#ifdef BSAL_NODE_USE_MESSAGE_RECYCLING
    bsal_fast_ring_init(&worker->outbound_buffers, capacity, sizeof(void *));
#endif

    bsal_ring_queue_init(&worker->scheduling_queue, sizeof(struct bsal_actor *));
    bsal_map_init(&worker->actors, sizeof(int), sizeof(int));
    bsal_map_iterator_init(&worker->actor_iterator, &worker->actors);

    bsal_fast_ring_init(&worker->outbound_message_queue, capacity, sizeof(struct bsal_message));

    bsal_ring_queue_init(&worker->outbound_message_queue_buffer, sizeof(struct bsal_message));


    worker->debug = 0;
    worker->busy = 0;

    worker->epoch_used_nanoseconds = 0;
    worker->loop_used_nanoseconds = 0;
    worker->scheduling_epoch_used_nanoseconds = 0;

    worker->start = 0;

/* 2 MiB is the default size for Linux huge pages.
 * \see https://wiki.debian.org/Hugepages
 * \see http://lwn.net/Articles/376606/
 */

    /*
     * 8 MiB
     */
    ephemeral_memory_block_size = 8388608;
    bsal_memory_pool_init(&worker->ephemeral_memory, ephemeral_memory_block_size);
    bsal_memory_pool_disable_tracking(&worker->ephemeral_memory);

    bsal_lock_init(&worker->lock);
    bsal_set_init(&worker->evicted_actors, sizeof(int));

    bsal_memory_pool_init(&worker->outbound_message_memory_pool, 2097152);

    /*
     * Disable the pool so that it uses allocate and free
     * directly.
     */
    bsal_memory_pool_disable(&worker->outbound_message_memory_pool);

    worker->ticks_without_production = 0;
}

void bsal_worker_destroy(struct bsal_worker *worker)
{

    bsal_lock_destroy(&worker->lock);

    bsal_fast_ring_destroy(&worker->actors_to_schedule);

#ifdef BSAL_NODE_USE_MESSAGE_RECYCLING
    bsal_fast_ring_destroy(&worker->outbound_buffers);
#endif
    bsal_ring_queue_destroy(&worker->scheduling_queue);
    bsal_fast_ring_destroy(&worker->outbound_message_queue);
    bsal_ring_queue_destroy(&worker->outbound_message_queue_buffer);

    bsal_map_destroy(&worker->actors);
    bsal_map_iterator_destroy(&worker->actor_iterator);
    bsal_set_destroy(&worker->evicted_actors);

    worker->node = NULL;

    worker->name = -1;
    worker->dead = 1;

    bsal_memory_pool_destroy(&worker->ephemeral_memory);
    bsal_memory_pool_destroy(&worker->outbound_message_memory_pool);

}

struct bsal_node *bsal_worker_node(struct bsal_worker *worker)
{
    return worker->node;
}

void bsal_worker_send(struct bsal_worker *worker, struct bsal_message *message)
{
    struct bsal_message copy;
    void *buffer;
    int count;
    int metadata_size;
    int all;
    void *old_buffer;

    memcpy(&copy, message, sizeof(struct bsal_message));
    count = bsal_message_count(&copy);
    metadata_size = bsal_message_metadata_size(message);
    all = count + metadata_size;

    /* use slab allocator to allocate buffer... */
    buffer = (char *)bsal_memory_pool_allocate(&worker->outbound_message_memory_pool,
                    all * sizeof(char));

#ifdef BSAL_WORKER_DEBUG_MEMORY
    printf("ALLOCATE %p\n", buffer);
#endif

#ifdef BSAL_WORKER_DEBUG
    printf("[bsal_worker_send] allocated %i bytes (%i + %i) for buffer %p\n",
                    all, count, metadata_size, buffer);

    printf("bsal_worker_send old buffer: %p\n",
                    bsal_message_buffer(message));
#endif

    /* according to
     * http://stackoverflow.com/questions/3751797/can-i-call-memcpy-and-memmove-with-number-of-bytes-set-to-zero
     * memcpy works with a count of 0, but the addresses must be valid
     * nonetheless
     *
     * Copy the message data.
     */
    if (count > 0) {
        old_buffer = bsal_message_buffer(message);
        memcpy(buffer, old_buffer, count);

        /* TODO use slab allocator */
    }

    bsal_message_set_buffer(&copy, buffer);
    bsal_message_write_metadata(&copy);

#ifdef BSAL_WORKER_DEBUG_20140601
    if (bsal_message_tag(message) == 1100) {
        printf("DEBUG bsal_worker_send 1100\n");
    }
#endif

    /* if the destination is on the same node,
     * handle that directly here to avoid locking things
     * with the node.
     */

    bsal_worker_enqueue_message(worker, &copy);
}

void bsal_worker_start(struct bsal_worker *worker, int processor)
{
    bsal_thread_init(&worker->thread, bsal_worker_main, worker);

    bsal_thread_set_affinity(&worker->thread, processor);

    bsal_thread_start(&worker->thread);

    worker->last_report = time(NULL);
    worker->epoch_start_in_nanoseconds = bsal_timer_get_nanoseconds();
    worker->loop_start_in_nanoseconds = worker->epoch_start_in_nanoseconds;
    worker->loop_end_in_nanoseconds = worker->loop_start_in_nanoseconds;
    worker->scheduling_epoch_start_in_nanoseconds = worker->epoch_start_in_nanoseconds;
}

void *bsal_worker_main(void *worker1)
{
    struct bsal_worker *worker;

    worker = (struct bsal_worker*)worker1;

#ifdef BSAL_WORKER_DEBUG
    bsal_worker_display(worker);
    printf("Starting worker thread\n");
#endif

    while (!worker->dead) {

        bsal_worker_run(worker);
    }

    return NULL;
}

void bsal_worker_display(struct bsal_worker *worker)
{
    printf("[bsal_worker_main] node %i worker %i\n",
                    bsal_node_name(worker->node),
                    bsal_worker_name(worker));
}

int bsal_worker_name(struct bsal_worker *worker)
{
    return worker->name;
}

void bsal_worker_stop(struct bsal_worker *worker)
{
#ifdef BSAL_WORKER_DEBUG
    bsal_worker_display(worker);
    printf("stopping worker!\n");
#endif

    /*
     * worker->dead is changed and will be read
     * by the running thread.
     */
    worker->dead = 1;

    bsal_thread_join(&worker->thread);

    worker->loop_end_in_nanoseconds = bsal_timer_get_nanoseconds();
}

int bsal_worker_is_busy(struct bsal_worker *self)
{
    return self->busy;
}


int bsal_worker_get_scheduled_message_count(struct bsal_worker *self)
{
    int value;
    struct bsal_map_iterator map_iterator;
    int actor_name;
    int messages;
    struct bsal_actor *actor;

    bsal_map_iterator_init(&map_iterator, &self->actors);

    value = 0;
    while (bsal_map_iterator_get_next_key_and_value(&map_iterator, &actor_name, NULL)) {

        actor = bsal_node_get_actor_from_name(self->node, actor_name);

        if (actor == NULL) {
            continue;
        }

        messages = bsal_actor_get_mailbox_size(actor);
        value += messages;
    }

    bsal_map_iterator_destroy(&map_iterator);

    return value;
}

float bsal_worker_get_epoch_load(struct bsal_worker *self)
{
    return self->epoch_load;
}

float bsal_worker_get_loop_load(struct bsal_worker *self)
{
    float loop_load;
    uint64_t elapsed_from_start;
    uint64_t current_nanoseconds;

    current_nanoseconds = self->loop_end_in_nanoseconds;
    elapsed_from_start = current_nanoseconds - self->loop_start_in_nanoseconds;

    /* This code path is currently not implemented when using
     * only 1 thread
     */
    if (elapsed_from_start == 0) {
        return 0;
    }

    loop_load = (0.0 + self->loop_used_nanoseconds) / elapsed_from_start;

    /* Avoid negative zeros
     */
    if (loop_load == 0) {
        loop_load = 0;
    }

    return loop_load;
}

struct bsal_memory_pool *bsal_worker_get_ephemeral_memory(struct bsal_worker *worker)
{
    return &worker->ephemeral_memory;
}

/* This can only be called from the CONSUMER
 */
int bsal_worker_dequeue_actor(struct bsal_worker *worker, struct bsal_actor **actor)
{
    int value;
    int name;
    struct bsal_actor *other_actor;
    int other_name;
    int operations;
    int status;
    int mailbox_size;

    operations = 4;

    /* Move an actor from the ring to the real actor scheduling queue
     */
    while (operations--
                    && bsal_fast_ring_pop_from_consumer(&worker->actors_to_schedule, &other_actor)) {

        other_name = bsal_actor_get_name(other_actor);

#ifdef BSAL_WORKER_DEBUG_SCHEDULER
        printf("ring.DEQUEUE %d\n", other_name);
#endif

        if (bsal_set_find(&worker->evicted_actors, &other_name)) {

#ifdef BSAL_WORKER_DEBUG_SCHEDULER
            printf("ALREADY EVICTED\n");
#endif
            continue;
        }

        if (!bsal_map_get_value(&worker->actors, &other_name, &status)) {
            /* Add the actor to the list of actors.
             * This does nothing if it is already in the list.
             */

            status = STATUS_IDLE;
            bsal_map_add_value(&worker->actors, &other_name, &status);

            bsal_map_iterator_destroy(&worker->actor_iterator);
            bsal_map_iterator_init(&worker->actor_iterator, &worker->actors);
        }

        /* If the actor is not queued, queue it
         */
        if (status == STATUS_IDLE) {
            status = STATUS_QUEUED;

            bsal_map_update_value(&worker->actors, &other_name, &status);

            bsal_ring_queue_enqueue(&worker->scheduling_queue, &other_actor);
        } else {

#ifdef BSAL_WORKER_DEBUG_SCHEDULER
            printf("SCHEDULER %d already scheduled to run, scheduled: %d\n", other_name,
                            (int)bsal_set_size(&worker->queued_actors));
#endif
        }
    }

    /* Now, dequeue an actor from the real queue.
     * If it has more than 1 message, re-enqueue it
     */
    value = bsal_ring_queue_dequeue(&worker->scheduling_queue, actor);

    if (value) {
        name = bsal_actor_get_name(*actor);

#ifdef BSAL_WORKER_DEBUG_SCHEDULER
        printf("scheduler.DEQUEUE actor %d, removed from queued actors...\n", name);
#endif

        mailbox_size = bsal_actor_get_mailbox_size(*actor);

        /* The actor has only one message and it is going to
         * be processed now.
         */
        if (mailbox_size == 1) {
#ifdef BSAL_WORKER_DEBUG_SCHEDULER
            printf("SCHEDULER %d has no message to schedule...\n", name);
#endif
            /* Set the status of the worker to STATUS_IDLE
             *
             * TODO: the ring new tail might not be visible too.
             * That could possibly be a problem...
             */
            status = STATUS_IDLE;
            bsal_map_update_value(&worker->actors, &name, &status);

        /* The actor still has a lot of messages
         * to process. Keep them coming.
         */
        } else if (mailbox_size >= 2) {

            /* Add the actor to the scheduling queue if it
             * still has messages
             */

#ifdef BSAL_WORKER_DEBUG_SCHEDULER
            printf("Scheduling actor %d again, messages: %d\n",
                        name,
                        bsal_actor_get_mailbox_size(*actor));
#endif

            /* The status is still STATUS_QUEUED
             */
            bsal_ring_queue_enqueue(&worker->scheduling_queue, actor);


        /* The actor is scheduled to run, but the new tail is not
         * yet visible apparently.
         *
         * Solution, push back the actor in the scheduler queue, it can take a few cycles to see cache changes across cores. (MESIF protocol)
         *
         * This is done below.
         */
        } else /* if (mailbox_size == 0) */ {

            status = STATUS_IDLE;
            bsal_map_update_value(&worker->actors, &name, &status);

            value = 0;
        }

    }

    /*
     * If no actor is scheduled to run, things are getting out of hand
     * and this is bad for business.
     *
     * So, here, an actor is poked for inactivity
     */
    if (!value) {
        ++worker->ticks_without_production;
    } else {
        worker->ticks_without_production = 0;
    }

    if (worker->ticks_without_production >= BSAL_WORKER_UNPRODUCTIVE_TICK_LIMIT) {

        if (bsal_map_iterator_get_next_key_and_value(&worker->actor_iterator, &name, NULL)) {

            other_actor = bsal_node_get_actor_from_name(worker->node, name);

            mailbox_size = 0;
            if (other_actor != NULL) {
                mailbox_size = bsal_actor_get_mailbox_size(other_actor);
            }

            if (mailbox_size > 0) {
                bsal_ring_queue_enqueue(&worker->scheduling_queue, &other_actor);

                status = STATUS_QUEUED;
                bsal_map_update_value(&worker->actors, &name, &status);
            }
        } else {

            /* Rewind the iterator.
             */
            bsal_map_iterator_destroy(&worker->actor_iterator);
            bsal_map_iterator_init(&worker->actor_iterator, &worker->actors);

            worker->ticks_without_production = 0;
        }
    }

    return value;
}

/* This can only be called from the PRODUCER
 */
int bsal_worker_enqueue_actor(struct bsal_worker *worker, struct bsal_actor **actor)
{
    return bsal_fast_ring_push_from_producer(&worker->actors_to_schedule, actor);
}

int bsal_worker_enqueue_message(struct bsal_worker *worker, struct bsal_message *message)
{

    /* Try to push the message in the output ring
     */
    if (!bsal_fast_ring_push_from_producer(&worker->outbound_message_queue, message)) {

        /* If that does not work, push the message in the queue buffer.
         */
        bsal_ring_queue_enqueue(&worker->outbound_message_queue_buffer, message);

    }

    return 1;
}

int bsal_worker_dequeue_message(struct bsal_worker *worker, struct bsal_message *message)
{
    int answer;

    answer = bsal_fast_ring_pop_from_consumer(&worker->outbound_message_queue, message);

    if (answer) {
        bsal_message_set_worker(message, worker->name);
    }

    return answer;
}

void bsal_worker_print_actors(struct bsal_worker *worker, struct bsal_scheduler *scheduler)
{
    struct bsal_map_iterator iterator;
    int name;
    int count;
    struct bsal_actor *actor;
    int producers;
    int consumers;
    int received;
    int difference;

    bsal_map_iterator_init(&iterator, &worker->actors);

    printf("worker/%d %d queued messages, received: %d busy: %d load: %f ring: %d scheduled actors: %d/%d\n",
                    bsal_worker_name(worker),
                    bsal_worker_get_scheduled_message_count(worker),
                    bsal_worker_get_sum_of_received_actor_messages(worker),
                    bsal_worker_is_busy(worker),
                    bsal_worker_get_scheduling_epoch_load(worker),
                    bsal_fast_ring_size_from_producer(&worker->actors_to_schedule),
                    bsal_ring_queue_size(&worker->scheduling_queue),
                    (int)bsal_map_size(&worker->actors));

    while (bsal_map_iterator_get_next_key_and_value(&iterator, &name, NULL)) {

        actor = bsal_node_get_actor_from_name(worker->node, name);

        if (actor == NULL) {
            continue;
        }

        count = bsal_actor_get_mailbox_size(actor);
        received = bsal_actor_get_sum_of_received_messages(actor);
        producers = bsal_map_size(bsal_actor_get_received_messages(actor));
        consumers = bsal_map_size(bsal_actor_get_sent_messages(actor));
        difference = bsal_scheduler_get_actor_production(scheduler, actor);

        printf("  [%s/%d] mailbox: %d received: %d (+%d) producers: %d consumers: %d\n",
                        bsal_actor_get_description(actor),
                        name, count, received,
                       difference,
                       producers, consumers);
    }


    /*printf("\n");*/
    bsal_map_iterator_destroy(&iterator);
}

void bsal_worker_evict_actor(struct bsal_worker *worker, int actor_name)
{
    int count;
    struct bsal_actor *actor;
    int name;

    bsal_set_add(&worker->evicted_actors, &actor_name);
    bsal_map_delete(&worker->actors, &actor_name);

    count = bsal_ring_queue_size(&worker->scheduling_queue);

    /* evict the actor from the scheduling queue
     */
    while (count--
                    && bsal_ring_queue_dequeue(&worker->scheduling_queue, &actor)) {

        name = bsal_actor_get_name(actor);

        if (name != actor_name) {

            bsal_ring_queue_enqueue(&worker->scheduling_queue,
                            &actor);
        }
    }

    /* Evict the actor from the ring
     */

    count = bsal_fast_ring_size_from_consumer(&worker->actors_to_schedule);

    while (count-- && bsal_fast_ring_pop_from_consumer(&worker->actors_to_schedule,
                            &actor)) {

        name = bsal_actor_get_name(actor);

        if (name != actor_name) {

            bsal_fast_ring_push_from_producer(&worker->actors_to_schedule,
                            &actor);
        }
    }

    bsal_map_iterator_destroy(&worker->actor_iterator);
    bsal_map_iterator_init(&worker->actor_iterator, &worker->actors);
}

void bsal_worker_lock(struct bsal_worker *worker)
{
    bsal_lock_lock(&worker->lock);
}

void bsal_worker_unlock(struct bsal_worker *worker)
{
    bsal_lock_unlock(&worker->lock);
}

struct bsal_map *bsal_worker_get_actors(struct bsal_worker *worker)
{
    return &worker->actors;
}

int bsal_worker_enqueue_actor_special(struct bsal_worker *worker, struct bsal_actor **actor)
{
    int name;

    name = bsal_actor_get_name(*actor);

    bsal_set_delete(&worker->evicted_actors, &name);

    return bsal_worker_enqueue_actor(worker, actor);
}

int bsal_worker_get_sum_of_received_actor_messages(struct bsal_worker *self)
{
    int value;
    struct bsal_map_iterator iterator;
    int actor_name;
    int messages;
    struct bsal_actor *actor;

    bsal_map_iterator_init(&iterator, &self->actors);

    value = 0;
    while (bsal_map_iterator_get_next_key_and_value(&iterator, &actor_name, NULL)) {

        actor = bsal_node_get_actor_from_name(self->node, actor_name);

        if (actor == NULL) {
            continue;
        }

        messages = bsal_actor_get_sum_of_received_messages(actor);

        value += messages;
    }

    bsal_map_iterator_destroy(&iterator);

    return value;
}

int bsal_worker_get_queued_messages(struct bsal_worker *self)
{
    int value;
    struct bsal_map_iterator map_iterator;
    int actor_name;
    int messages;
    struct bsal_actor *actor;

    bsal_map_iterator_init(&map_iterator, &self->actors);

    value = 0;
    while (bsal_map_iterator_get_next_key_and_value(&map_iterator, &actor_name, NULL)) {

        actor = bsal_node_get_actor_from_name(self->node, actor_name);

        if (actor == NULL) {
            continue;
        }

        messages = bsal_actor_get_mailbox_size(actor);

        value += messages;
    }

    bsal_map_iterator_destroy(&map_iterator);

    return value;

}

float bsal_worker_get_scheduling_epoch_load(struct bsal_worker *worker)
{
    uint64_t end_time;
    uint64_t period;

    end_time = bsal_timer_get_nanoseconds();

    period = end_time - worker->scheduling_epoch_start_in_nanoseconds;

    if (period == 0) {
        return 0;
    }

    return (0.0 + worker->scheduling_epoch_used_nanoseconds) / period;
}

void bsal_worker_reset_scheduling_epoch(struct bsal_worker *worker)
{
    worker->scheduling_epoch_start_in_nanoseconds = bsal_timer_get_nanoseconds();

    worker->scheduling_epoch_used_nanoseconds = 0;
}

int bsal_worker_get_production(struct bsal_worker *worker, struct bsal_scheduler *scheduler)
{
    struct bsal_map_iterator iterator;
    int name;
    struct bsal_actor *actor;
    int production;

    production = 0;
    bsal_map_iterator_init(&iterator, &worker->actors);

    while (bsal_map_iterator_get_next_key_and_value(&iterator, &name, NULL)) {

        actor = bsal_node_get_actor_from_name(worker->node, name);

        if (actor == NULL) {
            continue;
        }

        production += bsal_scheduler_get_actor_production(scheduler, actor);

    }

    bsal_map_iterator_destroy(&iterator);

    return production;
}

int bsal_worker_get_producer_count(struct bsal_worker *worker, struct bsal_scheduler *scheduler)
{
    struct bsal_map_iterator iterator;
    int name;
    struct bsal_actor *actor;
    int count;

    count = 0;
    bsal_map_iterator_init(&iterator, &worker->actors);

    while (bsal_map_iterator_get_next_key_and_value(&iterator, &name, NULL)) {

        actor = bsal_node_get_actor_from_name(worker->node, name);

        if (actor == NULL) {
            continue;
        }

        if (bsal_scheduler_get_actor_production(scheduler, actor) > 0) {
            ++count;
        }

    }

    bsal_map_iterator_destroy(&iterator);
    return count;
}

int bsal_worker_free_buffer(struct bsal_worker *worker, void *buffer)
{
#ifdef BSAL_WORKER_DEBUG_MEMORY
    printf("FREE %p\n", buffer);
#endif

#ifdef BSAL_NODE_USE_MESSAGE_RECYCLING
    return bsal_fast_ring_push_from_producer(&worker->outbound_buffers, &buffer);
#else
    bsal_memory_pool_free(&worker->outbound_message_memory_pool, buffer);

    return 1;
#endif
}

void bsal_worker_free_message(struct bsal_worker *worker, struct bsal_message *message)
{
    int source_worker;
    void *buffer;

    buffer = bsal_message_buffer(message);
    source_worker = bsal_message_get_worker(message);

    if (1 || source_worker == worker->name) {

        /* This is from the current worker
         */
        bsal_memory_pool_free(&worker->outbound_message_memory_pool, buffer);

#if 0
    } else if (source_worker >= 0) {

        bsal_message_recycle(message);

        bsal_worker_enqueue_message(worker, message);
#endif

    } else {

        /* This is from another fellow local worker
         * or from another BIOSAL node altogether.
         */
        bsal_message_recycle(message);

        bsal_worker_enqueue_message(worker, message);
    }
}
