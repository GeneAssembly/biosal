#!/bin/bash

duration=5

lttng create
lttng enable-event -u thorium_node:tick_enter,thorium_node:tick_exit --filter " node == 1"
echo "Tracing for $duration seconds"
lttng start
sleep $duration
lttng stop
lttng view > node-enter-exit.trace
wc -l node-enter-exit.trace
lttng destroy

