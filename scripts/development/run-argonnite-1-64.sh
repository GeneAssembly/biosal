#!/bin/bash

mpiexec -n 1 applications/argonnite_kmer_counter/argonnite -print-thorium-data -k 43 -threads-per-node 64 ~/dropbox/GPIC.1424-1.1371.fastq
