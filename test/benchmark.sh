#!/bin/bash

REPS=2

if (( $# < 1 )); then
    echo "Usage: $0 INFILE"
    exit 1
fi

INFILE=$1

for w in 1 2 4 8 16; do
    for m in 1 4 16 64 128 256 512 1024; do
        echo "${w} ${m}"
        for i in $(seq 1 ${REPS}); do
            # 1 CPU per worker + 1 for the rest
            taskset -c 0-$((w)) \
                ./benchmark ${INFILE} ${w} ${m} -t
        done
    done
done
