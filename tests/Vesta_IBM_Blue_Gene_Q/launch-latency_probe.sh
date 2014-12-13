#!/bin/bash

center=alcf
system=vesta
nodes=512
threads=16
walltime=00:30:00

build_script=scripts/IBM_Blue_Gene_Q/build-xl.sh
app_path=performance/latency_probe/latency_probe
job_template=tests/Mira_IBM_Blue_Gene_Q/Template.latency_probe.sh

root=/home/boisvert/vesta-tests
dataset=/dev/null

submit_command=bash

source tests/submit-actor-job-to-scheduler.sh

