
#include "mock.h"

#include <biosal.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * spawn one mock actor on the node and start
 * the node
 */
int main(int argc, char **argv)
{
    struct mock mock1;
    struct bsal_node node;

    bsal_node_init(&node, &argc, &argv);
    bsal_node_spawn(&node, &mock1, &mock_vtable);
    bsal_node_start(&node);
    bsal_node_destroy(&node);

    return 0;
}
