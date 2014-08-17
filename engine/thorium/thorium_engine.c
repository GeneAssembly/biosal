
#include "thorium_engine.h"

void bsal_thorium_engine_init(struct bsal_thorium_engine *self, int *argc, char ***argv)
{
    thorium_node_init(&self->node, argc, argv);
}

void bsal_thorium_engine_destroy(struct bsal_thorium_engine *self)
{
    thorium_node_destroy(&self->node);
}

int bsal_thorium_engine_boot(struct bsal_thorium_engine *self, int script_identifier,
                struct thorium_script *script)
{
    /*
     * Register the script
     */
    thorium_node_add_script(&self->node, script_identifier, script);

    /*
     * Spawn the initial actor
     */
    thorium_node_spawn(&self->node, script_identifier);

    /*
     * Boot the Thorium node
     */
    return thorium_node_run(&self->node);
}


int bsal_thorium_engine_boot_initial_actor(int *argc, char ***argv, int script_identifier, struct thorium_script *script)
{
    struct bsal_thorium_engine thorium_engine;
    int return_value;

#if 0
    int script_identifier;

    script_identifier = thorium_script_identifier(script);
#endif

    bsal_thorium_engine_init(&thorium_engine, argc, argv);
    return_value = bsal_thorium_engine_boot(&thorium_engine, script_identifier, script);
    bsal_thorium_engine_destroy(&thorium_engine);

    return return_value;
}
