
#include "node.h"

#include <time.h>
#include <stdio.h>
#include <inttypes.h>


int bsal_node_pull(struct bsal_node *node, struct bsal_message *message)
{
    return bsal_worker_pool_pull(&node->worker_pool, message);
}

void bsal_node_run_loop(struct bsal_node *node)
{
    struct bsal_message message;
    int credits;
    const int starting_credits = 1000;

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
    int ticks;
    int period;
    clock_t current_time;
    char print_information = 0;

    if (node->print_load || node->print_memory_usage) {
        print_information = 1;
    }

    period = BSAL_NODE_LOAD_PERIOD;
    ticks = 0;
#endif

    credits = starting_credits;

    while (credits > 0) {

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
        if (print_information) {
            current_time = time(NULL);

            if (current_time - node->last_report_time >= period) {
                if (node->print_load) {
                    bsal_worker_pool_print_load(&node->worker_pool, BSAL_WORKER_POOL_LOAD_EPOCH);
                    printf("ACTORS node %d has %d active actors\n", node->name,
                                    node->alive_actors);
                }

                if (node->print_memory_usage) {
                    printf("MEMORY %d s node/%d %" PRIu64 " bytes\n",
                                    (int)(current_time - node->start_time),
                                    bsal_node_name(node),
                                    bsal_get_heap_size());
                }
                node->last_report_time = current_time;
            }
        }
#endif

#ifdef BSAL_NODE_DEBUG_LOOP
        if (ticks % 1000000 == 0) {
            bsal_node_print_counters(node);
        }
#endif

#ifdef BSAL_NODE_DEBUG_LOOP1
        if (node->debug) {
            printf("DEBUG node/%d is running\n",
                            bsal_node_name(node));
        }
#endif

        /* pull message from network and assign the message to a thread.
         * this code path will call lock if
         * there is a message received.
         */
        if (
#ifdef BSAL_NODE_CHECK_MPI
            node->use_mpi &&
#endif
            bsal_transport_receive(&node->transport, &message)) {

            bsal_counter_add(&node->counter, BSAL_COUNTER_RECEIVED_MESSAGES_NOT_FROM_SELF, 1);
            bsal_counter_add(&node->counter, BSAL_COUNTER_RECEIVED_BYTES_NOT_FROM_SELF,
                    bsal_message_count(&message));


            bsal_node_dispatch_message(node, &message);
        }

        /* the one worker works here if there is only
         * one thread
         */
        if (node->worker_in_main_thread) {
            bsal_worker_pool_run(&node->worker_pool);
        }

        /* with 3 or more threads, the sending operations are
         * in another thread */
        if (!node->send_in_thread) {
            bsal_node_send_message(node);
        }

#ifdef BSAL_NODE_ENABLE_LOAD_REPORTING
        ticks++;
#endif

        --credits;

        /* if the node is still running, allocate new credits
         * to the engine loop
         */
        if (credits == 0) {
            if (bsal_node_running(node)) {
                credits = starting_credits;
            }
        }
    }

#ifdef BSAL_NODE_DEBUG_20140601_8
    printf("DEBUG node/%d exited loop\n",
                    bsal_node_name(node));
#endif
}

void bsal_node_send_message(struct bsal_node *node)
{
    struct bsal_message message;

#ifdef BSAL_NODE_CHECK_MPI
    /* Free buffers of active requests
     */
    if (node->use_mpi) {
#endif
        bsal_transport_test_requests(&node->transport);

#ifdef BSAL_NODE_CHECK_MPI
    }
#endif

    /* check for messages to send from from threads */
    /* this call lock only if there is at least
     * a message in the FIFO
     */
    if (bsal_node_pull(node, &message)) {

#ifdef BSAL_NODE_DEBUG
        printf("bsal_node_run pulled tag %i buffer %p\n",
                        bsal_message_tag(&message),
                        bsal_message_buffer(&message));
#endif

        /* send it locally or over the network */
        bsal_node_send(node, &message);
    }
}
