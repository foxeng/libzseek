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
 * Decompress the contents of @p cfilename and compare with @p ufilename.
 */
static bool decompress(const char *ufilename, const char *cfilename)
{
    // TODO: Access randomly, don't just scan.

    FILE *fin = NULL;
    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_reader_t *reader = NULL;
    size_t buf_len = 0;
    void *ubuf = NULL;
    void *dbuf = NULL;
    size_t offset = 0;

    fin = fopen(ufilename, "rb");
    if (!fin) {
        perror("decompress: open uncompressed file");
        goto fail;
    }

    reader = zseek_reader_open(cfilename, errbuf);
    if (!reader) {
        fprintf(stderr, "decompress: zseek_reader_open: %s\n", errbuf);
        goto fail_w_fin;
    }

    buf_len = BUF_SIZE;
    ubuf = malloc(buf_len);
    if (!ubuf) {
        perror("decompress: allocate buffer");
        goto fail_w_reader;
    }
    dbuf = malloc(buf_len);
    if (!ubuf) {
        perror("decompress: allocate decompression buffer");
        goto fail_w_ubuf;
    }


    offset = 0;
    do {
        size_t uread = 0;
        size_t to_read = 0;

        uread = fread(ubuf, 1, buf_len, fin);
        if (uread < buf_len && ferror(fin)) {
            perror("decompress: read uncompressed file");
            goto fail_w_dbuf;
        }

        to_read = uread;
        while (to_read > 0) {
            ssize_t dread = 0;

            dread = zseek_pread(reader, (uint8_t*)dbuf + (offset % buf_len),
                to_read, offset, errbuf);
            if (dread == -1) {
                fprintf(stderr, "decompress: zseek_pread: %s\n", errbuf);
                goto fail_w_dbuf;
            }
            to_read -= dread;
            offset += dread;

            if (dread == 0 && to_read > 0) {
                fprintf(stderr,
                    "decompress: unexpected EOF in compressed file\n");
                goto fail_w_dbuf;
            }
        }

        if (memcmp(ubuf, dbuf, buf_len)) {
            printf("FAIL: decompressed differs somewhere after byte %zu\n",
                offset);
            goto fail_w_dbuf;
        }
    } while (!feof(fin));


    free(dbuf);
    free(ubuf);

    if (!zseek_reader_close(reader, errbuf)) {
        fprintf(stderr, "decompress: zseek_reader_close: %s\n", errbuf);
        goto fail_w_fin;
    }

    if (fclose(fin) == EOF) {
        perror("decompress: close input file");
        goto fail;
    }

    return true;

fail_w_dbuf:
    free(dbuf);
fail_w_ubuf:
    free(ubuf);
fail_w_reader:
    zseek_reader_close(reader, errbuf);
fail_w_fin:
    fclose(fin);
fail:
    return false;
}

/**
 * Compress the contents of @p ufilename to @p cfilename.
 */
static bool compress(const char *ufilename, const char *cfilename)
{
    FILE *fin = NULL;
    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_writer_t *writer = NULL;
    size_t buf_len = 0;
    void *buf = NULL;

    fin = fopen(ufilename, "rb");
    if (!fin) {
        perror("compress: open uncompressed file");
        goto fail;
    }

    writer = zseek_writer_open(cfilename, NB_WORKERS, MIN_FRAME_SIZE, errbuf);
    if (!writer) {
        fprintf(stderr, "compress: zseek_writer_open: %s\n", errbuf);
        goto fail_w_fin;
    }

    buf_len = BUF_SIZE;
    buf = malloc(buf_len);
    if (!buf) {
        perror("compress: allocate buffer");
        goto fail_w_writer;
    }


    do {
        size_t uread = fread(buf, 1, buf_len, fin);
        if (uread < buf_len && ferror(fin)) {
            perror("compress: read file");
            goto fail_w_buf;
        }

        if (!zseek_write(writer, buf, uread, errbuf)) {
            fprintf(stderr, "compress: zseek_write: %s\n", errbuf);
            goto fail_w_buf;
        }
    } while (!feof(fin));


    free(buf);

    if (!zseek_writer_close(writer, errbuf)) {
        fprintf(stderr, "compress: zseek_writer_close: %s\n", errbuf);
        goto fail_w_fin;
    }

    if (fclose(fin) == EOF) {
        perror("compress: close input file");
        goto fail;
    }

    return true;

fail_w_buf:
    free(buf);
fail_w_writer:
    zseek_writer_close(writer, errbuf);
fail_w_fin:
    fclose(fin);
fail:
    return false;
}

int main(int argc, char *argv[])
{
    const char *ufilename = NULL;
    char *cfilename = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s INFILE\n", argv[0]);
        goto fail;
    }

    ufilename = argv[1];
    cfilename = malloc(strlen(ufilename) + 5);
    if (!cfilename) {
        perror("allocate output filename");
        goto fail;
    }
    strcpy(cfilename, ufilename);
    strcat(cfilename, ".zst");

    if (!compress(ufilename, cfilename))
        goto fail_w_cfilename;

    if (!decompress(ufilename, cfilename))
        goto fail_w_cfilename;

    printf("SUCCESS\n");

    free(cfilename);

    return 0;

fail_w_cfilename:
    free(cfilename);
fail:
    return 1;
}
