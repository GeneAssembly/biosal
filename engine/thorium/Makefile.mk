
THORIUM_OBJECTS=

# main objects for the actor system
THORIUM_OBJECTS += engine/thorium/message.o
THORIUM_OBJECTS += engine/thorium/message_block.o
THORIUM_OBJECTS += engine/thorium/node.o
THORIUM_OBJECTS += engine/thorium/actor.o
THORIUM_OBJECTS += engine/thorium/script.o
THORIUM_OBJECTS += engine/thorium/worker.o
THORIUM_OBJECTS += engine/thorium/worker_debugger.o
THORIUM_OBJECTS += engine/thorium/worker_pool.o
THORIUM_OBJECTS += engine/thorium/thorium_engine.o
THORIUM_OBJECTS += engine/thorium/dispatcher.o
THORIUM_OBJECTS += engine/thorium/route.o
THORIUM_OBJECTS += engine/thorium/worker_buffer.o
THORIUM_OBJECTS += engine/thorium/actor_profiler.o

# actor modules. These are mostly traits.
THORIUM_OBJECTS += engine/thorium/modules/binomial_tree_message.o
THORIUM_OBJECTS += engine/thorium/modules/proxy_message.o
THORIUM_OBJECTS += engine/thorium/modules/actions.o
THORIUM_OBJECTS += engine/thorium/modules/send_helpers.o
THORIUM_OBJECTS += engine/thorium/modules/active_message_limit.o
THORIUM_OBJECTS += engine/thorium/modules/stop.o
THORIUM_OBJECTS += engine/thorium/modules/time_in_seconds.o
THORIUM_OBJECTS += engine/thorium/modules/log.o
THORIUM_OBJECTS += engine/thorium/modules/adaptive_actor.o
THORIUM_OBJECTS += engine/thorium/modules/message_helper.o

include engine/thorium/scheduler/Makefile.mk

include engine/thorium/transport/Makefile.mk

THORIUM_OBJECTS += engine/thorium/cache/cache_tag.o
THORIUM_OBJECTS += engine/thorium/cache/message_cache.o
THORIUM_OBJECTS += engine/thorium/cache/cache_actor_adapter.o

THORIUM_OBJECTS += engine/thorium/topology/router.o
THORIUM_OBJECTS += engine/thorium/topology/polytope.o
THORIUM_OBJECTS += engine/thorium/topology/tuple.o

LIBRARY_OBJECTS += $(THORIUM_OBJECTS)
