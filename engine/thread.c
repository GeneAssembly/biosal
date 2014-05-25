
#include "thread.h"
#include "work.h"
#include "message.h"
#include "node.h"

#include <stdlib.h>

void bsal_thread_init(struct bsal_thread *thread, struct bsal_node *node)
{
    bsal_fifo_init(&thread->inbound_messages, 16, sizeof(struct bsal_work));
    bsal_fifo_init(&thread->outbound_messages, 16, sizeof(struct bsal_message));

    thread->node = node;
}

void bsal_thread_destroy(struct bsal_thread *thread)
{
    bsal_fifo_destroy(&thread->outbound_messages);
    bsal_fifo_destroy(&thread->inbound_messages);
}

struct bsal_fifo *bsal_thread_inbound_messages(struct bsal_thread *thread)
{
    return &thread->inbound_messages;
}

struct bsal_fifo *bsal_thread_outbound_messages(struct bsal_thread *thread)
{
    return &thread->outbound_messages;
}

void bsal_thread_run(struct bsal_thread *thread)
{
    struct bsal_work work;

    /* check for messages in inbound FIFO */
    if (bsal_fifo_pop(&thread->inbound_messages, &work)) {

        /* dispatch message to a worker thread (currently, this is the main thread) */
        bsal_thread_work(thread, &work);
    }
}

void bsal_thread_work(struct bsal_thread *thread, struct bsal_work *work)
{
    bsal_actor_receive_fn_t receive;
    struct bsal_actor *actor;
    struct bsal_message *message;

    actor = bsal_work_actor(work);

    bsal_actor_lock(actor);

    bsal_actor_set_thread(actor, thread);
    message = bsal_work_message(work);

    receive = bsal_actor_get_receive(actor);

    receive(actor, message);

    bsal_actor_set_thread(actor, NULL);
    int dead = bsal_actor_dead(actor);

    if (dead) {
        bsal_node_notify_death(thread->node, actor);
    }

    bsal_actor_unlock(actor);

    /* TODO free the buffer with the slab allocator */
    /* TODO replace with slab allocator */
    free((void*)message);
}

struct bsal_node *bsal_thread_node(struct bsal_thread *thread)
{
    return thread->node;
}

void bsal_thread_send(struct bsal_thread *thread, struct bsal_message *message)
{
    bsal_fifo_push(bsal_thread_outbound_messages(thread), message);
}

void bsal_thread_receive(struct bsal_thread *thread, struct bsal_message *message)
{
    bsal_fifo_push(bsal_thread_inbound_messages(thread), message);
}
