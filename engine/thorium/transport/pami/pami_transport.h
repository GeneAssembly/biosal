
#ifndef BSAL_PAMI_TRANSPORT_H
#define BSAL_PAMI_TRANSPORT_H

/*
 * Decide if the code will use PAMI or MPI
 */

/*
 * This variable is set to 1 if PAMI support is ready to be
 * used
 */
#define BSAL_TRANSPORT_PAMI_IS_READY 0

#define BSAL_TRANSPORT_PAMI_IDENTIFIER 2000
#define BSAL_TRANSPORT_PAMI_NAME "PAMI: Parallel Active Message Interface"

/*
 * Use IBM PAMI on IBM Blue Gene/Q
 * PAMI: Parallel Active Message Interface
 */
#if defined(__bgq__) && BSAL_TRANSPORT_PAMI_IS_READY

#define BSAL_TRANSPORT_USE_PAMI

#endif


#if defined(BSAL_TRANSPORT_USE_PAMI)

#include <pami.h>

#endif

struct bsal_node;
struct bsal_message;
struct bsal_active_buffer;
struct bsal_transport;

struct bsal_pami_transport {
#ifdef BSAL_TRANSPORT_USE_PAMI
    pami_client_t client;
#endif
    int mock;
};

void bsal_pami_transport_init(struct bsal_transport *self, int *argc, char ***argv);
void bsal_pami_transport_destroy(struct bsal_transport *self);

int bsal_pami_transport_send(struct bsal_transport *self, struct bsal_message *message);
int bsal_pami_transport_receive(struct bsal_transport *self, struct bsal_message *message);

int bsal_pami_transport_get_identifier(struct bsal_transport *self);
const char *bsal_pami_transport_get_name(struct bsal_transport *self);

#endif
