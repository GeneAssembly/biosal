#!/bin/bash

lttng-gen-tp engine/thorium/tracepoints/lttng/message.tp
lttng-gen-tp engine/thorium/tracepoints/lttng/actor.tp
lttng-gen-tp engine/thorium/tracepoints/lttng/ring.tp

#-std=c99
#
CFLAGS=" -O3 -march=x86-64 -g -I."
CFLAGS+=" -DTHORIUM_DEBUG"
CFLAGS+=" -D THORIUM_USE_LTTNG"
LDFLAGS=" -llttng-ust -ldl -lm -lz"
clear
echo "CFLAGS: $CFLAGS"

lttng-gen-tp engine/thorium/tracepoints/lttng/message.tp

make clean
make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" all -j 8 THORIUM_USE_LTTNG=y
