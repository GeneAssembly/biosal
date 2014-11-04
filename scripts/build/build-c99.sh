#!/bin/bash

# George's script

CFLAGS="-O3 -march=x86-64 -g -std=c99 -Wall -I. -D_POSIX_C_SOURCE=200112L "
clear
echo "CFLAGS: $CFLAGS"

make clean
make CFLAGS="$CFLAGS" all -j 8 CONFIG_DEBUG=y
