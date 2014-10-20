
THORIUM_OBJECTS += engine/thorium/tracepoints/message_tracepoints.o
THORIUM_OBJECTS += engine/thorium/tracepoints/actor_tracepoints.o
THORIUM_OBJECTS += engine/thorium/tracepoints/node_tracepoints.o
THORIUM_OBJECTS += engine/thorium/tracepoints/tracepoint_session.o

THORIUM_OBJECTS-$(THORIUM_USE_LTTNG) += engine/thorium/tracepoints/lttng/message.o
THORIUM_OBJECTS-$(THORIUM_USE_LTTNG) += engine/thorium/tracepoints/lttng/ring.o
THORIUM_OBJECTS-$(THORIUM_USE_LTTNG) += engine/thorium/tracepoints/lttng/actor.o
THORIUM_OBJECTS-$(THORIUM_USE_LTTNG) += engine/thorium/tracepoints/lttng/transport.o
THORIUM_OBJECTS-$(THORIUM_USE_LTTNG) += engine/thorium/tracepoints/lttng/scheduler.o

