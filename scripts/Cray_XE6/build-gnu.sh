#!/bin/bash

module swap PrgEnv-cray/4.2.24 PrgEnv-gnu/4.2.24

make clean
make CC=cc -j 4 applications/argonnite_kmer_counter/argonnite CONFIG_FLAGS="-DTHORIUM_DEBUG" \
        applications/spate_metagenome_assembler/spate
