#!/bin/bash

# -print-counters
mpiexec -n 1 applications/argonnite -k 43 -threads-per-node 1 ~/dropbox/mini.fastq
