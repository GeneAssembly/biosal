#!/bin/bash

mpiexec -n 1 applications/argonnite -print-structure -k 43 -threads-per-node 28 ~/dropbox/medium.fastq
