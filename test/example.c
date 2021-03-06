#include <stddef.h>     // size_t
#include <stdbool.h>    // bool
#include <stdint.h>     // uint*_t
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, free
#include <errno.h>      // perror
#include <string.h>     // strcpy, strcat, memcmp, strcmp
#include <assert.h>     // assert

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

    FILE *ufile = fopen(ufilename, "rb");
    if (!ufile) {
        perror("decompress: open uncompressed file");
        goto fail;
    }

    FILE *cfile = fopen(cfilename, "rb");
    if (!cfile) {
        perror("decompress: open compressed file");
        goto fail_w_ufile;
    }

    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_reader_t *reader = zseek_reader_open(cfile, 1, NULL, errbuf);
    if (!reader) {
        fprintf(stderr, "decompress: zseek_reader_open: %s\n", errbuf);
        goto fail_w_cfile;
    }

    size_t buf_len = BUF_SIZE;
    void *ubuf = malloc(buf_len);
    if (!ubuf) {
        perror("decompress: allocate buffer");
        goto fail_w_reader;
    }
    void *dbuf = malloc(buf_len);
    if (!dbuf) {
        perror("decompress: allocate decompression buffer");
        goto fail_w_ubuf;
    }


    size_t offset = 0;
    do {
        size_t uread = fread(ubuf, 1, buf_len, ufile);
        if (uread < buf_len && ferror(ufile)) {
            perror("decompress: read uncompressed file");
            goto fail_w_dbuf;
        }

        size_t to_read = uread;
        while (to_read > 0) {
            ssize_t dread = zseek_pread(reader,
                (uint8_t*)dbuf + (offset % buf_len), to_read, offset, NULL,
                    errbuf);
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

        if (memcmp(ubuf, dbuf, uread)) {
            printf("FAIL: decompressed differs somewhere before byte %zu\n",
                offset);
            goto fail_w_dbuf;
        }
    } while (!feof(ufile));


    free(dbuf);
    free(ubuf);

    if (!zseek_reader_close(reader, NULL, errbuf)) {
        fprintf(stderr, "decompress: zseek_reader_close: %s\n", errbuf);
        goto fail_w_cfile;
    }

    if (fclose(cfile) == EOF) {
        perror("decompress: close compressed file");
        goto fail_w_ufile;
    }

    if (fclose(ufile) == EOF) {
        perror("decompress: close uncompressed file");
        goto fail;
    }

    return true;

fail_w_dbuf:
    free(dbuf);
fail_w_ubuf:
    free(ubuf);
fail_w_reader:
    zseek_reader_close(reader, NULL, errbuf);
fail_w_cfile:
    fclose(cfile);
fail_w_ufile:
    fclose(ufile);
fail:
    return false;
}

/**
 * Compress the contents of @p ufilename to @p cfilename.
 */
static bool compress(const char *ufilename, const char *cfilename,
    zseek_compression_type_t ctype)
{
    FILE *ufile = fopen(ufilename, "rb");
    if (!ufile) {
        perror("compress: open uncompressed file");
        goto fail;
    }

    FILE *cfile = fopen(cfilename, "wb");
    if (!cfile) {
        perror("compress: open compressed file");
        goto fail_w_ufile;
    }

    char errbuf[ZSEEK_ERRBUF_SIZE];
    zseek_compression_param_t param = {0};
    switch (ctype) {
    case ZSEEK_ZSTD:
        param.type = ZSEEK_ZSTD;
        param.params.zstd_params.nb_workers = NB_WORKERS;
        param.params.zstd_params.compression_level = 3;
        param.params.zstd_params.strategy = 1;
        break;
    case ZSEEK_LZ4:
        param.type = ZSEEK_LZ4;
        param.params.lz4_params.compression_level = 0;
        break;
    default:
        // BUG
        assert(false);
        goto fail_w_cfile;
    }
    zseek_writer_t *writer = zseek_writer_open(cfile, &param, MIN_FRAME_SIZE,
        NULL, errbuf);
    if (!writer) {
        fprintf(stderr, "compress: zseek_writer_open: %s\n", errbuf);
        goto fail_w_cfile;
    }

    size_t buf_len = BUF_SIZE;
    void *buf = malloc(buf_len);
    if (!buf) {
        perror("compress: allocate buffer");
        goto fail_w_writer;
    }


    do {
        size_t uread = fread(buf, 1, buf_len, ufile);
        if (uread < buf_len && ferror(ufile)) {
            perror("compress: read file");
            goto fail_w_buf;
        }

        if (!zseek_write(writer, buf, uread, NULL, errbuf)) {
            fprintf(stderr, "compress: zseek_write: %s\n", errbuf);
            goto fail_w_buf;
        }
    } while (!feof(ufile));


    free(buf);

    if (!zseek_writer_close(writer, NULL, errbuf)) {
        fprintf(stderr, "compress: zseek_writer_close: %s\n", errbuf);
        goto fail_w_cfile;
    }

    if (fclose(cfile) == EOF) {
        perror("compress: close compressed file");
        goto fail_w_ufile;
    }

    if (fclose(ufile) == EOF) {
        perror("compress: close uncompressed file");
        goto fail;
    }

    return true;

fail_w_buf:
    free(buf);
fail_w_writer:
    zseek_writer_close(writer, NULL, errbuf);
fail_w_cfile:
    fclose(cfile);
fail_w_ufile:
    fclose(ufile);
fail:
    return false;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s --zstd|--lz4 INFILE\n", argv[0]);
        goto fail;
    }

    zseek_compression_type_t ctype;
    if (strcmp(argv[1], "--zstd") == 0)
        ctype = ZSEEK_ZSTD;
    else if (strcmp(argv[1], "--lz4") == 0)
        ctype = ZSEEK_LZ4;
    else {
        fprintf(stderr, "Usage: %s --zstd|--lz4 INFILE\n", argv[0]);
        goto fail;
    }

    const char *ufilename = argv[2];
    char *cfilename = malloc(strlen(ufilename) + 5);
    if (!cfilename) {
        perror("allocate output filename");
        goto fail;
    }
    strcpy(cfilename, ufilename);
    switch (ctype) {
    case ZSEEK_ZSTD:
        strcat(cfilename, ".zst");
        break;
    case ZSEEK_LZ4:
        strcat(cfilename, ".lz4");
        break;
    default:
        // BUG
        assert(false);
        goto fail_w_cfilename;
    }

    if (!compress(ufilename, cfilename, ctype))
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
