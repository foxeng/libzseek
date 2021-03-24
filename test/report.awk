#!/bin/awk -f

BEGIN {
    delete workers
    delete frames

    delete times    # Wall time
    delete cpu      # CPU usage
    delete tput_tot # Total throughput
    delete tput_pw  # Throughput per worker
    delete mem      # Max RSS
    delete lat_mean # Mean nio_archive_write() latency
    delete lat_max  # Max nio_archive_write() latency
}


NF == 2 {
    w = $1
    f = $2
    # Presence table for number of workers and minimum frame size
    workers[w] = 1
    frames[f] = 1

    # Reset counters
    tsum = 0
    csum = 0
    ttsum = 0
    twsum = 0
    msum = 0
    lmean = 0
    lmax = 0
    reps = 0
}

NF > 2 {
    tsum += $1      # Wall time (sec)
    csum += $5      # CPU usage (%)
    ttsum += $6     # Total throughput (MiB/sec)
    twsum += $7     # Throughput per worker (MiB/sec)
    msum += $8      # Max RSS (MiB)
    lmean += $9     # Mean latency (msec)
    lmax += $12     # Max latency (msec)
    reps++

    # Maintain running average
    times[w,f] = tsum / reps
    cpu[w,f] = csum / reps
    tput_tot[w,f] = ttsum / reps
    tput_pw[w,f] = twsum / reps
    mem[w,f] = msum / reps
    lat_mean[w,f] = lmean / reps
    lat_max[w,f] = lmax / reps
}


function print_horizontal(width,    i, f)
{
    # Print horizontal line
    for (i = 0; i < 6; i++)
        printf "-"
    for (f in frames)
        for (i = 0; i < width + 1; i++)
            printf "-"
    printf "\n"

    # Print horizontal axis
    printf "%3s | ", "f"
    for (f in frames)
        printf "%"width"d ", f
    printf "\n"
}

function print_wall(width,    w, f)
{
    printf "Wall time (sec)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width".2f ", times[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_cpu(width,    w, f)
{
    printf "CPU usage (%)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width"d ", cpu[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_tput_tot(width,    w, f)
{
    printf "Total throughput (MiB/sec)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width".1f ", tput_tot[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_tput_pw(width,    w, f)
{
    printf "Throughput per worker (MiB/sec)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width".1f ", tput_pw[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_mem(width,    w, f)
{
    printf "Max RSS (MiB)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width".1f ", mem[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_lat_mean(width,    w, f)
{
    printf "Mean nio_archive_write() latency (msec)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width"f ", lat_mean[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

function print_lat_max(width,    w, f)
{
    printf "Max nio_archive_write() latency (msec)\n"
    printf "%3s\n", "w"
    for (w in workers) {
        printf "%3d | ", w
        for (f in frames)
            printf "%"width"f ", lat_max[w,f]
        printf "\n"
    }

    print_horizontal(width)
}

END {
    print_wall(6)

    printf "\n"
    print_tput_tot(6)

    printf "\n"
    print_tput_pw(6)

    printf "\n"
    print_cpu(6)

    printf "\n"
    print_mem(6)
}
