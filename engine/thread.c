
#include "thread.h"

#include "work.h"
#include "message.h"
#include "node.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*#define BSAL_THREAD_DEBUG*/

void bsal_thread_init(struct bsal_thread *thread, int name, struct bsal_node *node)
{
    bsal_fifo_init(&thread->works, 16, sizeof(struct bsal_work));
    bsal_fifo_init(&thread->messages, 16, sizeof(struct bsal_message));

    thread->node = node;
    thread->name = name;
    thread->dead = 0;


#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_init(&thread->work_mutex, NULL);
    pthread_mutex_init(&thread->message_mutex, NULL);
#endif
}

void bsal_thread_destroy(struct bsal_thread *thread)
{
#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_destroy(&thread->message_mutex);
    pthread_mutex_destroy(&thread->work_mutex);
#endif

    thread->node = NULL;

    bsal_fifo_destroy(&thread->messages);
    bsal_fifo_destroy(&thread->works);
    thread->name = -1;
    thread->dead = 1;
}

struct bsal_fifo *bsal_thread_works(struct bsal_thread *thread)
{
    return &thread->works;
}

struct bsal_fifo *bsal_thread_messages(struct bsal_thread *thread)
{
    return &thread->messages;
}

void bsal_thread_run(struct bsal_thread *thread)
{
    struct bsal_work work;

    /* check for messages in inbound FIFO */
    if (bsal_thread_pull_work(thread, &work)) {

        /* dispatch message to a worker thread (currently, this is the main thread) */
        bsal_thread_work(thread, &work);
    }
}

void bsal_thread_work(struct bsal_thread *thread, struct bsal_work *work)
{
    struct bsal_actor *actor;
    struct bsal_message *message;
    bsal_actor_receive_fn_t receive;
    int dead;

    actor = bsal_work_actor(work);

    /* lock the actor to prevent another thread from making work
     * on the same actor at the same time
     */
    bsal_actor_lock(actor);

    bsal_actor_set_thread(actor, thread);
    message = bsal_work_message(work);
    receive = bsal_actor_get_receive(actor);
    receive(actor, message);

    bsal_actor_set_thread(actor, NULL);
    dead = bsal_actor_dead(actor);

    if (dead) {
        bsal_node_notify_death(thread->node, actor);
    }

    /* Unlock the actor.
     * This does not do anything if a death notification
     * was sent to the node
     */
    bsal_actor_unlock(actor);

    /* TODO free the buffer with the slab allocator */
    free(bsal_message_buffer(message));
    /* TODO replace with slab allocator */
    free(message);
}

struct bsal_node *bsal_thread_node(struct bsal_thread *thread)
{
    return thread->node;
}

void bsal_thread_send(struct bsal_thread *thread, struct bsal_message *message)
{
    struct bsal_message copy;
    char *buffer;
    int count;
    int metadata_size;
    int all;

    memcpy(&copy, message, sizeof(struct bsal_message));
    count = bsal_message_count(&copy);
    metadata_size = bsal_message_metadata_size(message);
    all = count + metadata_size;

    /* TODO use slab allocator to allocate buffer... */
    buffer = (char *)malloc(all * sizeof(char));

    /* according to
     * http://stackoverflow.com/questions/3751797/can-i-call-memcpy-and-memmove-with-number-of-bytes-set-to-zero
     * memcpy works with a count of 0, but the addresses must be valid
     * nonetheless
     *
     * Copy the message data.
     */
    if (count > 0) {
        memcpy(buffer, bsal_message_buffer(message), count);
    }

    bsal_message_set_buffer(&copy, buffer);
    bsal_message_write_metadata(&copy);

    bsal_thread_push_message(thread, &copy);
}

/*
void bsal_thread_receive(struct bsal_thread *thread, struct bsal_message *message)
{
    bsal_fifo_push(bsal_thread_works(thread), message);
}
*/

void bsal_thread_start(struct bsal_thread *thread)
{
    /*
     * http://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread_create.html
     */

    pthread_create(bsal_thread_thread(thread), NULL, bsal_thread_main,
                    thread);
}

void *bsal_thread_main(void *pointer)
{
    struct bsal_thread *thread;

    thread = (struct bsal_thread*)pointer;

#ifdef BSAL_THREAD_DEBUG
    bsal_thread_display(thread);
    printf("Starting thread\n");
#endif

    while (!thread->dead) {

        bsal_thread_run(thread);
    }

    return NULL;
}

void bsal_thread_display(struct bsal_thread *thread)
{
    printf("[bsal_thread_main] node %i thread %i\n",
                    bsal_node_rank(thread->node),
                    bsal_thread_name(thread));
}

int bsal_thread_name(struct bsal_thread *thread)
{
    return thread->name;
}

void bsal_thread_stop(struct bsal_thread *thread)
{
#ifdef BSAL_THREAD_DEBUG
    bsal_thread_display(thread);
    printf("stopping thread!\n");
#endif

    /*
     * thread->dead is volatile and will be read
     * by the running thread.
     */
    thread->dead = 1;

    /* http://man7.org/linux/man-pages/man3/pthread_join.3.html
     */
    pthread_join(thread->thread, NULL);
}

pthread_t *bsal_thread_thread(struct bsal_thread *thread)
{
    return &thread->thread;
}

void bsal_thread_push_work(struct bsal_thread *thread, struct bsal_work *work)
{
#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_lock(&thread->work_mutex);
#endif

    bsal_fifo_push(bsal_thread_works(thread), work);

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_unlock(&thread->work_mutex);
#endif
}

int bsal_thread_pull_work(struct bsal_thread *thread, struct bsal_work *work)
{
    int value;

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_lock(&thread->work_mutex);
#endif

    value = bsal_fifo_pop(bsal_thread_works(thread), work);

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_unlock(&thread->work_mutex);
#endif

    return value;
}

void bsal_thread_push_message(struct bsal_thread *thread, struct bsal_message *message)
{
#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_lock(&thread->message_mutex);
#endif

    bsal_fifo_push(bsal_thread_messages(thread), message);

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_unlock(&thread->message_mutex);
#endif
}

int bsal_thread_pull_message(struct bsal_thread *thread, struct bsal_message *message)
{
    int value;

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_lock(&thread->message_mutex);
#endif

    value = bsal_fifo_pop(bsal_thread_messages(thread), message);

#ifdef BSAL_THREAD_USE_MUTEX
    pthread_mutex_unlock(&thread->message_mutex);
#endif

    return value;
}
