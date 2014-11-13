#!/bin/bash

# echo "Commit= __COMMIT__"

qsub \
 --env PAMID_THREAD_MULTIPLE=1 \
 -A CompBIO \
 -n 1024 \
 -t 01:00:00 \
 -O __JOB__ \
 --mode c1 \
     __JOB__.__APP__ -print-load -threads-per-node 48
