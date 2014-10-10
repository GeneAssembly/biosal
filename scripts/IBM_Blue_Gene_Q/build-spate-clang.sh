#!/bin/bash

# on Cetus:
# 1. edit ~/.soft
# 2. run 'resoft'

CFLAGS="-I. -g -O3 -DTHORIUM_DEBUG "
CFLAGS="$CFLAGS -I/soft/libraries/alcf/current/xl/ZLIB/include"
LDFLAGS="-L/soft/libraries/alcf/current/xl/ZLIB/lib -lm -lz"

make clean
make -j 8 CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" \
    applications/spate_metagenome_assembler/spate
