#include <stddef.h>     // size_t
#include <stdint.h>     // uint*_t
#include <stdbool.h>    // bool
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // perror
#include <string.h>     // strcpy, strcat, memcmp
#include <math.h>       // sqrt
#include <time.h>       // clock_gettime

#include <sys/stat.h>   // stat
#include <sys/time.h>
#include <sys/resource.h>   // getrusage

#include <zseek.h>

#define CHUNK_SIZE (1 << 20)  // 1 MiB

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct results {
    off_t usize;
    struct timespec wt1;
    struct timespec wt2;
    struct rusage ru1;
    struct rusage ru2;
    double *latencies;
    size_t num_latencies;
} results_t;

static results_t *results_new(size_t latencies_capacity)
{
    double *latencies = malloc(latencies_capacity * sizeof(*latencies));
    if (!latencies)
        goto fail;

    results_t *res = malloc(sizeof(*res));
    if (!res)
        goto fail_w_latencies;
    memset(res, 0, sizeof(*res));
    res->latencies = latencies;

    return res;

fail_w_latencies:
    free(latencies);
fail:
    return NULL;
}

static void results_free(results_t *res)
{
    if (!res)
        return;

    free(res->latencies);
    free(res);
}

static void report(const results_t *r, int nb_workers, bool terse)
{
    // Wall time
    double wt = difftime(r->wt2.tv_sec, r->wt1.tv_sec) +
        (r->wt2.tv_nsec - r->wt1.tv_nsec) / (1000.0 * 1000 * 1000);

    struct timeval ut1 = r->ru1.ru_utime;
    struct timeval ut2 = r->ru2.ru_utime;
    // User time
    double ut = difftime(ut2.tv_sec, ut1.tv_sec) +
        (ut2.tv_usec - ut1.tv_usec) / (1000.0 * 1000);
    struct timeval st1 = r->ru1.ru_stime;
    struct timeval st2 = r->ru2.ru_stime;
    // System time
    double st = difftime(st2.tv_sec, st1.tv_sec) +
        (st2.tv_usec - st1.tv_usec) / (1000.0 * 1000);
    // CPU time
    double ct = ut + st;

    // CPU usage
    double cu = 100 * (ct / wt);

    // Total throughput
    double tput_tot = (r->usize / (1 << 20)) / wt;

    // Throughput per worker
    double tput_pw = tput_tot / nb_workers;

    // Max RSS
    double mem = (r->ru2.ru_maxrss - r->ru1.ru_maxrss) / (double)(1 << 10);

    // Latency (wall) min, max, mean and standard deviation
    double lat_min = r->latencies[0];
    double lat_max = r->latencies[0];
    double sum = 0;
    for (size_t i = 0; i < r->num_latencies; i++) {
        if (r->latencies[i] < lat_min)
            lat_min = r->latencies[i];
        if (r->latencies[i] > lat_max)
            lat_max = r->latencies[i];
        sum += r->latencies[i];
    }
    double lat_mean = sum / r->num_latencies;
    sum = 0;
    for (size_t i = 0; i < r->num_latencies; i++)
        sum += (r->latencies[i] - lat_mean) * (r->latencies[i] - lat_mean);
    double lat_std = sqrt(sum / r->num_latencies);


    if (terse)
        printf("%.2lf %.2lf %.2lf %.2lf %.0lf %.2lf %.2lf %.0lf %lf %lf %lf %lf\n",
            wt, ct, ut, st, cu, tput_tot, tput_pw, mem, lat_mean, lat_std,
            lat_min, lat_max);
    else {
        printf("Wall time (sec): %.2lf\n", wt);
        printf("CPU time (sec): %.2lf (%.2lf + %.2lf)\n", ct, ut, st);
        printf("CPU usage: %.0lf%%\n", cu);
        printf("Throughput (MiB/sec): %.2lf (%.2lf per worker)\n", tput_tot,
            tput_pw);
        printf("Max RSS: %.0lf (MiB)\n", mem);
        printf("zseek_write() latency (msec): %lf +- %lf [%lf, %lf]\n",
            lat_mean, lat_std, lat_min, lat_max);
    }
}

/**
 * Compress the contents of @p ufilename to @p cfilename.
 */
static results_t *compress(const char *ufilename, const char *cfilename,
    int nb_workers, size_t min_frame_size)
{
    // TODO OPT: mmap?

    // Read whole file into memory
    struct stat st;
    if (stat(ufilename, &st) == -1) {
        perror("compress: get uncompressed info");
        goto fail;
    }
    off_t usize = st.st_size;

    size_t buf_len = usize;
    results_t *res = results_new((buf_len / CHUNK_SIZE) + 1);
    if (!res) {
        perror("compress: allocate results");
        goto fail;
    }
    res->usize = usize;

    FILE *fin = fopen(ufilename, "rb");
    if (!fin) {
        perror("compress: open uncompressed file");
        goto fail_w_res;
    }

    void *buf = malloc(buf_len);
    if (!buf) {
        perror("compress: allocate buffer");
        goto fail_w_fin;
    }

    if (fread(buf, 1, buf_len, fin) != buf_len) {
        perror("compress: read file");
        goto fail_w_buf;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &res->wt1) == -1) {
        perror("compress: get wall time");
        goto fail_w_buf;
    }
    if (getrusage(RUSAGE_SELF, &res->ru1) == -1) {
        perror("compress: get resource usage");
        goto fail_w_buf;
    }

    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_writer_t *writer = zseek_writer_open(cfilename, nb_workers,
        min_frame_size, errbuf);
    if (!writer) {
        fprintf(stderr, "compress: zseek_writer_open: %s\n", errbuf);
        goto fail_w_buf;
    }

    for (off_t fpos = 0; fpos < (off_t)buf_len; fpos += CHUNK_SIZE) {
        struct timespec t1;
        if (clock_gettime(CLOCK_MONOTONIC, &t1) == -1) {
            perror("compress: get inside wall time");
            goto fail_w_writer;
        }

        size_t len = MIN(buf_len - fpos, CHUNK_SIZE);
        if (!zseek_write(writer, (uint8_t*)buf + fpos, len, errbuf)) {
            fprintf(stderr, "compress: zseek_write: %s\n", errbuf);
            goto fail_w_writer;
        }

        struct timespec t2;
        if (clock_gettime(CLOCK_MONOTONIC, &t2) == -1) {
            perror("compress: get inside wall time");
            goto fail_w_writer;
        }
        double lat = difftime(t2.tv_sec, t1.tv_sec) * 1000;    // msec
        lat += (t2.tv_nsec - t1.tv_nsec) / (1000.0 * 1000);
        res->latencies[res->num_latencies++] = lat;
    }

    if (!zseek_writer_close(writer, errbuf)) {
        fprintf(stderr, "compress: zseek_writer_close: %s\n", errbuf);
        goto fail_w_buf;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &res->wt2) == -1) {
        perror("compress: get wall time");
        goto fail_w_buf;
    }
    if (getrusage(RUSAGE_SELF, &res->ru2) == -1) {
        perror("compress: get resource usage");
        goto fail_w_buf;
    }


    free(buf);

    if (fclose(fin) == EOF) {
        perror("compress: close input file");
        goto fail_w_res;
    }

    return res;

fail_w_writer:
    zseek_writer_close(writer, errbuf);
fail_w_buf:
    free(buf);
fail_w_fin:
    fclose(fin);
fail_w_res:
    results_free(res);
fail:
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s INFILE nb_workers frame_size (MiB) [-t]\n",
            argv[0]);
        return 1;
    }

    const char *ufilename = argv[1];
    char *cfilename = malloc(strlen(ufilename) + 5);
    if (!cfilename) {
        perror("allocate output filename");
        goto fail;
    }
    strcpy(cfilename, ufilename);
    strcat(cfilename, ".zst");

    int nb_workers = atoi(argv[2]);
    size_t frame_size = atoi(argv[3]) * (1 << 20);

    results_t *res = compress(ufilename, cfilename, nb_workers, frame_size);
    if (!res)
        goto fail_w_cfilename;

    bool terse = false;
    if (argc > 4 && strcmp(argv[4], "-t") == 0) {
        terse = true;
    }
    report(res, nb_workers, terse);

    results_free(res);
    free(cfilename);

    return 0;

fail_w_cfilename:
    free(cfilename);
fail:
    return 1;
}
