#!/bin/bash

make -j applications/spate_metagenome_assembler/spate

if ! test -d velvet
then
    git clone https://github.com/dzerbino/velvet.git
fi

if test -d spate_output
then
    rm -rf spate_output
fi

mpiexec -n 1 applications/spate_metagenome_assembler/spate -k 25 -threads-per-node 2 \
    velvet/data/test_reads.fa \
    -enable-actor-log all \
    -o spate_output \
