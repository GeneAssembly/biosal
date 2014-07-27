
#include "pami_transport.h"

#include <engine/thorium/transport/transport.h>
#include <engine/thorium/message.h>

#include <core/system/debugger.h>

void bsal_pami_transport_init(struct bsal_transport *transport, int *argc, char ***argv)
{
#ifdef BSAL_TRANSPORT_USE_PAMI
    const char client_name[] = "biosal/thorium";
    struct bsal_pami_transport *pami_transport;
    int configuration_count;
    pami_result_t result;
    pami_configuration_t *configurations;
    pami_configuration_t query_configurations[3];
    size_t contexts;

    configuration_count = 0;
    configurations = NULL;

    pami_transport = bsal_transport_get_concrete_transport(transport);

    /*
     * \see http://www-01.ibm.com/support/knowledgecenter/SSFK3V_1.3.0/com.ibm.cluster.protocols.v1r3.pp400.doc/bl510_pclientc.htm
     */
    result = PAMI_Client_create(client_name, &pami_transport->client, configurations,
                    configuration_count);

    BSAL_DEBUGGER_ASSERT(result == PAMI_SUCCESS);

    if (result != PAMI_SUCCESS) {
        return;
    }

    query_configurations[0].name = PAMI_CLIENT_NUM_TASKS;
    query_configurations[1].name = PAMI_CLIENT_TASK_ID;
    query_configurations[2].name = PAMI_CLIENT_NUM_CONTEXTS;

    result = PAMI_Client_query(pami_transport->client, query_configurations, 3);
    transport->size = query_configurations[0].value.intval;
    transport->rank = query_configurations[1].value.intval;
    contexts = query_configurations[2].value.intval;

    BSAL_DEBUGGER_ASSERT(contexts > 1);
#endif
}

void bsal_pami_transport_destroy(struct bsal_transport *transport)
{
#ifdef BSAL_TRANSPORT_USE_PAMI
    struct bsal_pami_transport *pami_transport;
    pami_result_t result;

    pami_transport = bsal_transport_get_concrete_transport(transport);

    result = PAMI_Client_destroy(&pami_transport->client);

    if (result != PAMI_SUCCESS) {
        return;
    }
#endif
}

int bsal_pami_transport_send(struct bsal_transport *transport, struct bsal_message *message)
{
    /*
     * Based on this example: http://code.google.com/p/pami-examples/source/browse/trunk/function/send.c
     */
    int destination_node;
    void *buffer;

    destination_node = bsal_message_destination_node(message);
    buffer = bsal_message_buffer(message);


    /*
     * Send the data to the destination with PAMI
     */

    return 0;
}

int bsal_pami_transport_receive(struct bsal_transport *transport, struct bsal_message *message)
{
    return 0;
}


