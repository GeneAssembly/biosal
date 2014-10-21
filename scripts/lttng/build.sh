#!/bin/bash

make clean

for i in engine/thorium/tracepoints/lttng/*.tp
do
    lttng-gen-tp $i
done

#-std=c99
#
CFLAGS=" -O3 -march=x86-64 -g -I."
CFLAGS+=" -DTHORIUM_DEBUG"
CFLAGS+=" -D THORIUM_USE_LTTNG"
LDFLAGS=" -llttng-ust -ldl -lm -lz"
clear
echo "CFLAGS: $CFLAGS"

make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" all -j 8 THORIUM_USE_LTTNG=y
