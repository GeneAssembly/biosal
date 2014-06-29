
#include "transport.h"
#include "message.h"
#include "node.h"
#include "active_buffer.h"

#include <system/memory.h>

/*
 * \see http://www.mpich.org/static/docs/v3.1/www3/MPI_Comm_dup.html
 * \see http://www.dartmouth.edu/~rc/classes/intro_mpi/hello_world_ex.html
 * \see https://github.com/GeneAssembly/kiki/blob/master/ki.c#L960
 * \see http://mpi.deino.net/mpi_functions/MPI_Comm_create.html
 */
void bsal_transport_init(struct bsal_transport *self, struct bsal_node *node, int *argc,
                char ***argv)
{
    int required;

    self->node = node;

    /*
    required = MPI_THREAD_MULTIPLE;
    */
    required = MPI_THREAD_FUNNELED;

    MPI_Init_thread(argc, argv, required, &self->provided);

    /* make a new communicator for the library and don't use MPI_COMM_WORLD later */
    MPI_Comm_dup(MPI_COMM_WORLD, &self->comm);
    MPI_Comm_rank(self->comm, &self->rank);
    MPI_Comm_size(self->comm, &self->size);

    self->datatype = MPI_BYTE;
    bsal_queue_init(&self->active_buffers, sizeof(struct bsal_active_buffer));

}

void bsal_transport_destroy(struct bsal_transport *self)
{
    struct bsal_active_buffer active_buffer;

    while (bsal_queue_dequeue(&self->active_buffers, &active_buffer)) {
        bsal_active_buffer_destroy(&active_buffer);
    }

    bsal_queue_destroy(&self->active_buffers);

    self->node = NULL;
    MPI_Finalize();
}

/* \see http://www.mpich.org/static/docs/v3.1/www3/MPI_Isend.html */
void bsal_transport_send(struct bsal_transport *self, struct bsal_message *message)
{
    char *buffer;
    int count;
    /* int source; */
    int destination;
    int tag;
    int metadata_size;
    MPI_Request *request;
    int all;
    struct bsal_active_buffer active_buffer;

    buffer = bsal_message_buffer(message);
    count = bsal_message_count(message);
    metadata_size = bsal_message_metadata_size(message);
    all = count + metadata_size;
    destination = bsal_message_destination_node(message);
    tag = bsal_message_tag(message);

    bsal_active_buffer_init(&active_buffer, buffer);
    request = bsal_active_buffer_request(&active_buffer);

    /* TODO get return value */
    MPI_Isend(buffer, all, self->datatype, destination, tag,
                    self->comm, request);

    /* store the MPI_Request to test it later to know when
     * the buffer can be reused
     */
    /* \see http://blogs.cisco.com/performance/mpi_request_free-is-evil/
     */
    /*MPI_Request_free(&request);*/

    bsal_queue_enqueue(&self->active_buffers, &active_buffer);
}

/* \see http://www.mpich.org/static/docs/v3.1/www3/MPI_Iprobe.html */
/* \see http://www.mpich.org/static/docs/v3.1/www3/MPI_Recv.html */
/* \see http://www.malcolmmclean.site11.com/www/MpiTutorial/MPIStatus.html */
int bsal_transport_receive(struct bsal_transport *self, struct bsal_message *message)
{
    char *buffer;
    int count;
    int source;
    int destination;
    int tag;
    int flag;
    int metadata_size;
    MPI_Status status;

    source = MPI_ANY_SOURCE;
    tag = MPI_ANY_TAG;
    destination = self->rank;

    /* TODO get return value */
    MPI_Iprobe(source, tag, self->comm, &flag, &status);

    if (!flag) {
        return 0;
    }

    /* TODO get return value */
    MPI_Get_count(&status, self->datatype, &count);

    /* TODO actually allocate (slab allocator) a buffer with count bytes ! */
    buffer = (char *)bsal_memory_allocate(count * sizeof(char));

    source = status.MPI_SOURCE;
    tag = status.MPI_TAG;

    /* TODO get return value */
    MPI_Recv(buffer, count, self->datatype, source, tag,
                    self->comm, &status);

    metadata_size = bsal_message_metadata_size(message);
    count -= metadata_size;

    /* Initially assign the MPI source rank and MPI destination
     * rank for the actor source and actor destination, respectively.
     * Then, read the metadata and resolve the MPI rank from
     * that. The resolved MPI ranks should be the same in all cases
     */
    bsal_message_init(message, tag, count, buffer);
    bsal_message_set_source(message, destination);
    bsal_message_set_destination(message, destination);
    bsal_message_read_metadata(message);
    bsal_transport_resolve(self, message);

    return 1;
}

void bsal_transport_resolve(struct bsal_transport *self, struct bsal_message *message)
{
    int actor;
    int node_name;
    struct bsal_node *node;

    node = self->node;

    actor = bsal_message_source(message);
    node_name = bsal_node_actor_node(node, actor);
    bsal_message_set_source_node(message, node_name);

    actor = bsal_message_destination(message);
    node_name = bsal_node_actor_node(node, actor);
    bsal_message_set_destination_node(message, node_name);
}

int bsal_transport_get_provided(struct bsal_transport *self)
{
    return self->provided;
}

int bsal_transport_get_rank(struct bsal_transport *self)
{
    return self->rank;
}

int bsal_transport_get_size(struct bsal_transport *self)
{
    return self->size;
}

void bsal_transport_test_requests(struct bsal_transport *self)
{
    struct bsal_active_buffer active_buffer;
    void *buffer;

    if (bsal_queue_dequeue(&self->active_buffers, &active_buffer)) {

        if (bsal_active_buffer_test(&active_buffer)) {
            buffer = bsal_active_buffer_buffer(&active_buffer);

            /* TODO use an allocator
             */
            bsal_memory_free(buffer);

            bsal_active_buffer_destroy(&active_buffer);

        /* Just put it back in the FIFO for later */
        } else {
            bsal_queue_enqueue(&self->active_buffers, &active_buffer);
        }
    }
}


