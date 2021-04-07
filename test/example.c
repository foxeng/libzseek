#include <stddef.h>     // size_t
#include <stdbool.h>    // bool
#include <stdint.h>     // uint*_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // perror
#include <string.h>     // strcpy, strcat, memcmp

#include <zseek.h>

#define BUF_SIZE (1 << 12)  // 4 KiB
#define NB_WORKERS 1
#define MIN_FRAME_SIZE (1 << 20)    // 1 MiB

/**
 * Decompress the contents of @cfilename and compare with @ufilename.
 */
static bool decompress(const char *ufilename, const char *cfilename)
{
    // TODO: Access randomly, don't just scan.

    FILE *fin = fopen(ufilename, "rb");
    if (!fin) {
        perror("decompress: open uncompressed file");
        return false;
    }

    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_reader_t *reader = zseek_reader_open(cfilename, errbuf);
    if (!reader) {
        fprintf(stderr, "decompress: zseek_reader_open failed\n");
        return false;
    }

    size_t buf_len = BUF_SIZE;
    void *ubuf = malloc(buf_len);
    if (!ubuf) {
        perror("decompress: allocate buffer");
        return false;
    }
    void *dbuf = malloc(buf_len);
    if (!ubuf) {
        perror("decompress: allocate decompression buffer");
        return false;
    }


    size_t offset = 0;
    do {
        size_t uread = fread(ubuf, 1, buf_len, fin);
        if (uread < buf_len && ferror(fin)) {
            perror("decompress: read uncompressed file");
            return false;
        }

        size_t to_read = uread;
        while (to_read > 0) {
            ssize_t dread = zseek_pread(reader, (uint8_t*)dbuf + (offset % buf_len),
                to_read, offset, errbuf);
            if (dread == -1) {
                fprintf(stderr, "decompress: zseek_pread failed\n");
                return false;
            }
            to_read -= dread;
            offset += dread;

            if (dread == 0 && to_read > 0) {
                fprintf(stderr,
                    "decompress: unexpected EOF in compressed file\n");
                return false;
            }
        }

        if (memcmp(ubuf, dbuf, buf_len)) {
            printf("FAIL: decompressed differs somewhere after byte %zu\n",
                offset);
            return false;
        }
    } while (!feof(fin));


    free(dbuf);
    free(ubuf);

    if (fclose(fin) == EOF) {
        perror("decompress: close input file");
        return false;
    }

    return true;
}

/**
 * Compress the contents of @ufilename to @cfilename.
 */
static bool compress(const char *ufilename, const char *cfilename)
{
    FILE *fin = fopen(ufilename, "rb");
    if (!fin) {
        perror("compress: open uncompressed file");
        return false;
    }

    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_writer_t *writer = zseek_writer_open(cfilename, NB_WORKERS,
        MIN_FRAME_SIZE, errbuf);
    if (!writer) {
        fprintf(stderr, "compress: zseek_writer_open failed\n");
        return false;
    }

    size_t buf_len = BUF_SIZE;
    void *buf = malloc(buf_len);
    if (!buf) {
        perror("compress: allocate buffer");
        return false;
    }


    do {
        size_t uread = fread(buf, 1, buf_len, fin);
        if (uread < buf_len && ferror(fin)) {
            perror("compress: read file");
            return false;
        }

        if (!zseek_write(writer, buf, uread, errbuf)) {
            fprintf(stderr, "compress: zseek_write failed\n");
            return false;
        }
    } while (!feof(fin));


    free(buf);

    if (!zseek_writer_close(writer, errbuf)) {
        fprintf(stderr, "compress: zseek_writer_close failed\n");
        return false;
    }

    if (fclose(fin) == EOF) {
        perror("compress: close input file");
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s INFILE\n",
            argv[0]);
        return 1;
    }

    const char *ufilename = argv[1];
    char *cfilename = malloc(strlen(ufilename) + 5);
    if (!cfilename) {
        perror("allocate output filename");
        return 1;
    }
    strcpy(cfilename, ufilename);
    strcat(cfilename, ".zst");

    if (!compress(ufilename, cfilename))
        return 1;

    if (!decompress(ufilename, cfilename))
        return 1;

    printf("SUCCESS\n");
}
