
#include "worker.h"

#include "work.h"
#include "message.h"
#include "node.h"

#include <core/structures/map_iterator.h>
#include <core/structures/vector_iterator.h>

#include <core/helpers/vector_helper.h>

#include <core/system/timer.h>

#include <stdio.h>

/* Just return the number of queued messages.
 */
int bsal_worker_get_message_production_score(struct bsal_worker *self)
{
    int score;

    score = 0;

#ifdef BSAL_WORKER_USE_FAST_RINGS
    score += bsal_fast_ring_size_from_producer(&self->message_queue);
#else
    score += bsal_ring_size(&self->message_queue);
#endif

    score += bsal_ring_queue_size(&self->local_message_queue);

    return score;
}

#ifdef BSAL_WORKER_HAS_OWN_QUEUES
int bsal_worker_pull_message(struct bsal_worker *worker, struct bsal_message *message)
{
#ifdef BSAL_WORKER_USE_FAST_RINGS
    return bsal_fast_ring_pop_from_consumer(&worker->message_queue, message);
#else
    return bsal_ring_pop(&worker->message_queue, message);
#endif
}
#endif

int bsal_worker_pull_work(struct bsal_worker *worker, struct bsal_work *work)
{
#ifdef BSAL_WORKER_USE_FAST_RINGS
    int result;

    /* first, pull from the ring.
     * If that fails, pull from the local queue
     */
    result = bsal_fast_ring_pop_from_consumer(&worker->work_queue, work);
    if (result) {
        return result;
    }

    return bsal_worker_dequeue_work(worker, work);

#else
    return bsal_ring_pop(&worker->work_queue, work);
#endif
}

void bsal_worker_run(struct bsal_worker *worker)
{
    struct bsal_work work;
    struct bsal_message other_message;

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
    clock_t current_time;
    clock_t elapsed;
    int period;
    uint64_t current_nanoseconds;
    uint64_t start_time;
    uint64_t end_time;
    uint64_t elapsed_nanoseconds;
    uint64_t elapsed_from_start;
#endif

#ifdef BSAL_WORKER_DEBUG
    int tag;
    int destination;
    struct bsal_message *message;
#endif

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
    period = BSAL_NODE_LOAD_PERIOD;
    current_time = time(NULL);

    elapsed = current_time - worker->last_report;

    if (elapsed >= period) {

        current_nanoseconds = bsal_timer_get_nanoseconds();

#ifdef BSAL_WORKER_DEBUG_LOAD
        printf("DEBUG Updating load report\n");
#endif
        elapsed_nanoseconds = current_nanoseconds - worker->epoch_start_in_nanoseconds;
        elapsed_from_start = current_nanoseconds - worker->loop_start_in_nanoseconds;

        if (elapsed_nanoseconds > 0) {
            worker->epoch_load = (0.0 + worker->epoch_used_nanoseconds) / elapsed_nanoseconds;
            worker->loop_load = (0.0 + worker->loop_used_nanoseconds) / elapsed_from_start;

            /* \see http://stackoverflow.com/questions/9657993/negative-zero-in-c
             */
            if (worker->epoch_load == 0) {
                worker->epoch_load = 0;
            }
            if (worker->loop_load == 0) {
                worker->loop_load = 0;
            }
            worker->epoch_used_nanoseconds = 0;
            worker->epoch_start_in_nanoseconds = current_nanoseconds;
            worker->last_report = current_time;
        }
    }
#endif

#ifdef BSAL_WORKER_DEBUG
    if (worker->debug) {
        printf("DEBUG worker/%d bsal_worker_run\n",
                        bsal_worker_name(worker));
    }
#endif

    /* check for messages in inbound FIFO */
    if (bsal_worker_pull_work(worker, &work)) {

#ifdef BSAL_WORKER_DEBUG
        message = bsal_work_message(&work);
        tag = bsal_message_tag(message);
        destination = bsal_message_destination(message);

        if (tag == BSAL_ACTOR_ASK_TO_STOP) {
            printf("DEBUG pulled BSAL_ACTOR_ASK_TO_STOP for %d\n",
                            destination);
        }
#endif

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
        start_time = bsal_timer_get_nanoseconds();
#endif

        /* dispatch message to a worker */
        bsal_worker_work(worker, &work);

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
        end_time = bsal_timer_get_nanoseconds();

        elapsed_nanoseconds = end_time - start_time;
        worker->epoch_used_nanoseconds += elapsed_nanoseconds;
        worker->loop_used_nanoseconds += elapsed_nanoseconds;
#endif
    }

    /* queue buffered message
     */
    if (bsal_ring_queue_dequeue(&worker->local_message_queue, &other_message)) {

        bsal_worker_push_message(worker, &other_message);
    }
}

void bsal_worker_queue_work(struct bsal_worker *worker, struct bsal_work *work)
{
    int count;
    struct bsal_work local_work;
    struct bsal_actor *actor;
    int i;
    struct bsal_map frequencies;
    struct bsal_map_iterator iterator;
    int actor_name;
    struct bsal_vector counts;
    struct bsal_vector_iterator vector_iterator;
    int actor_works;

    /* just put it in the local queue.
     */
    bsal_worker_enqueue_work(worker, work);

    count = bsal_ring_queue_size(&worker->local_work_queue);

    /* evict actors too, if any...
    bsal_worker_evict_actors(worker);
     */

    if (count >= BSAL_WORKER_WARNING_THRESHOLD
                   && (worker->last_warning == 0
                           || count >= worker->last_warning + BSAL_WORKER_WARNING_THRESHOLD_STRIDE)) {


        bsal_map_init(&frequencies, sizeof(int), sizeof(int));

        printf("Warning: CONTENTION node %d, worker %d has %d works in its local queue.\n",
                        bsal_node_name(worker->node),
                        bsal_worker_name(worker),
                        count);

        worker->last_warning = count;

        printf("List: \n");

        i = 0;

        while (i < count && bsal_worker_dequeue_work(worker,
                                &local_work)) {

            actor = bsal_work_actor(&local_work);

            actor_name = bsal_actor_name(actor);

            printf("WORK node %d worker %d work %d/%d actor %d\n",
                            bsal_node_name(worker->node),
                            bsal_worker_name(worker),
                            i,
                            count,
                            actor_name);

            bsal_worker_enqueue_work(worker, &local_work);

            if (!bsal_map_get_value(&frequencies, &actor_name, &actor_works)) {
                actor_works = 0;
                bsal_map_add_value(&frequencies, &actor_name, &actor_works);
            }

            ++actor_works;
            bsal_map_update_value(&frequencies, &actor_name, &actor_works);

            ++i;
        }

        bsal_map_iterator_init(&iterator, &frequencies);
        bsal_vector_init(&counts, sizeof(int));

        while (bsal_map_iterator_get_next_key_and_value(&iterator, &actor_name, &actor_works)) {

            printf("actor %d ... %d messages\n",
                            actor_name, actor_works);

            bsal_vector_push_back(&counts, &actor_works);
        }

        bsal_map_iterator_destroy(&iterator);

        bsal_vector_helper_sort_int(&counts);

        bsal_vector_iterator_init(&vector_iterator, &counts);

        printf("Sorted counts:\n");
        while (bsal_vector_iterator_get_next_value(&vector_iterator, &actor_works)) {

            printf(" %d\n", actor_works);
        }

        bsal_vector_iterator_destroy(&vector_iterator);

        bsal_vector_destroy(&counts);
        bsal_map_destroy(&frequencies);
    }
}

void bsal_worker_work(struct bsal_worker *worker, struct bsal_work *work)
{
    struct bsal_actor *actor;
    struct bsal_message *message;
    int dead;
    char *buffer;

#ifdef BSAL_WORKER_DEBUG
    int actor_name;
    int tag;
    int destination;
#endif

    actor = bsal_work_actor(work);
    message = bsal_work_message(work);

#ifdef BSAL_WORKER_DEBUG
    tag = bsal_message_tag(message);
    destination = bsal_message_destination(message);

    if (tag == BSAL_ACTOR_ASK_TO_STOP) {
        printf("DEBUG bsal_worker_work BSAL_ACTOR_ASK_TO_STOP %d\n", destination);
    }
#endif

    /* Store the buffer location before calling the user
     * code because the user may change the message buffer.
     * We need to free the buffer regardless if the
     * actor code changes it.
     */
    buffer = bsal_message_buffer(message);

    /* the actor died while the work was queued.
     */
    if (bsal_actor_dead(actor)) {

        printf("NOTICE actor/%d is dead already (bsal_worker_work)\n",
                        bsal_message_destination(message));
        bsal_memory_free(buffer);
        bsal_memory_free(message);

        return;
    }

    /* lock the actor to prevent another worker from making work
     * on the same actor at the same time
     */
    if (bsal_actor_trylock(actor) != BSAL_LOCK_SUCCESS) {

        /*
        actor_name = bsal_actor_name(actor);

        printf("Warning: CONTENTION worker %d could not lock actor %d, returning the message...\n",
                        bsal_worker_name(worker),
                        actor_name);
                        */

        /* Send the message back to the
         * source.
         */
        bsal_worker_push_message(worker, message);

        /* do some eviction too right now.
         */

        /*bsal_worker_evict_actor(worker, actor_name);
        bsal_worker_evict_actors(worker);
        */

        return;
    }

    /* the actor died while this worker was waiting for the lock
     */
    if (bsal_actor_dead(actor)) {

        printf("DEBUG bsal_worker_work actor died while the worker was waiting for the lock.\n");
#ifdef BSAL_WORKER_DEBUG
#endif
        /* TODO free the buffer with the slab allocator */
        bsal_memory_free(buffer);

        /* TODO replace with slab allocator */
        bsal_memory_free(message);

        /*bsal_actor_unlock(actor);*/
        return;
    }

    /* call the actor receive code
     */
    bsal_actor_set_worker(actor, worker);
    bsal_actor_receive(actor, message);

    bsal_actor_set_worker(actor, NULL);

    /* Free ephemeral memory
     */
    bsal_memory_pool_free_all(&worker->ephemeral_memory);

    dead = bsal_actor_dead(actor);

    if (dead) {
        bsal_node_notify_death(worker->node, actor);
    }

#ifdef BSAL_WORKER_DEBUG_20140601
    if (worker->debug) {
        printf("DEBUG worker/%d after dead call\n",
                        bsal_worker_name(worker));
    }
#endif

    /* Unlock the actor.
     * This does not do anything if a death notification
     * was sent to the node
     */
    bsal_actor_unlock(actor);

#ifdef BSAL_WORKER_DEBUG
    printf("bsal_worker_work Freeing buffer %p %i tag %i\n",
                    buffer, bsal_message_count(message),
                    bsal_message_tag(message));
#endif

    /* TODO free the buffer with the slab allocator */

    /*printf("DEBUG182 Worker free %p\n", buffer);*/
    bsal_memory_free(buffer);

    /* TODO replace with slab allocator */
    bsal_memory_free(message);

#ifdef BSAL_WORKER_DEBUG_20140601
    if (worker->debug) {
        printf("DEBUG worker/%d exiting bsal_worker_work\n",
                        bsal_worker_name(worker));
    }
#endif
}


