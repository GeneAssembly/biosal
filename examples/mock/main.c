
#include "mock.h"

#include <biosal.h>

/*
 * spawn one mock actor on the node and start
 * the node
 */
int main(int argc, char **argv)
{
    return biosal_thorium_engine_boot_initial_actor(&argc, &argv, SCRIPT_MOCK, &mock_script);
}
