/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include <stdint.h>     // uint*_t
#include <stddef.h>     // size_t
#include <stdbool.h>    // bool
#include <limits.h>     // UINT_MAX
#include <stdio.h>      // I/O
#include <stdlib.h>     // malloc, realloc, free
#include <assert.h>     // assert
#include <string.h>     // memcpy

#include <endian.h>     // htole32, le32toh
#include <zstd.h>
#include <zstd_errors.h>

#include "seek_table.h"

#define ZSTD_seekTableFooterSize 9
#define ZSTD_SEEKABLE_MAGICNUMBER 0x8F92EAB1
#define ZSTD_SEEKABLE_MAXFRAMES 0x8000000U
#define ZSTD_SKIPPABLEHEADERSIZE 8

#define SEEKTABLE_SKIPPABLE_MAGICNUMBER (ZSTD_MAGIC_SKIPPABLE_START | 0xE)
#define SEEK_ENTRY_SIZE_NO_CHECKSUM 8
#define SEEK_ENTRY_CHECKSUM_SIZE 4
#define SEEKKTABLE_BUF_SIZE (1 << 12)   // 4KiB

#define CHECK_Z(f) { size_t const ret = (f); if (ret != 0) return ret; }
#define ERROR(name) ((size_t)-ZSTD_error_##name)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef uint8_t BYTE;
typedef uint32_t U32;
typedef uint64_t U64;

/* NOTE: The below two definitions are copied verbatim from
zstd/contrib/seekable_format/zstdseek_decompress.c @ v1.4.9 */

typedef struct {
    U64 cOffset;
    U64 dOffset;
    U32 checksum;
} seekEntry_t;

struct ZSTD_seekTable_s {
    seekEntry_t* entries;
    size_t tableLen;

    int checksumFlag;
};

static inline void MEM_writeLE32(void *memPtr, U32 val32)
{
    U32 val32le = htole32(val32);
    memcpy(memPtr, &val32le, sizeof(val32le));
}

static inline U32 MEM_readLE32(void const *memPtr)
{
    U32 val32le;
    memcpy(&val32le, memPtr, sizeof(val32le));
    return le32toh(val32le);
}

static bool read_st_entries(FILE *fin, seekEntry_t *entries, size_t num_entries,
    bool checksum)
{
    size_t entry_size = SEEK_ENTRY_SIZE_NO_CHECKSUM + \
        (checksum ? SEEK_ENTRY_CHECKSUM_SIZE : 0);
    size_t buf_len = SEEKKTABLE_BUF_SIZE - \
        (SEEKKTABLE_BUF_SIZE % entry_size);    // fit whole # of entries
    void *buf = malloc(buf_len);
    if (!buf)
        goto fail;

    size_t c_offset = 0;
    size_t d_offset = 0;
    size_t buf_idx = 0;
    for (size_t e = 0; e < num_entries; e++) {
        if (buf_idx == 0 || buf_idx == buf_len) {
            // Fill buffer
            size_t to_read = MIN((num_entries - e) * entry_size, buf_len);
            if (fread(buf, 1, to_read, fin) != to_read)
                goto fail_w_buf;
            buf_idx = 0;
        }

        // Parse entry
        entries[e].cOffset = c_offset;
        entries[e].dOffset = d_offset;
        c_offset += MEM_readLE32((uint8_t*)buf + buf_idx);
        buf_idx += 4;
        d_offset += MEM_readLE32((uint8_t*)buf + buf_idx);
        buf_idx += 4;
        if (checksum) {
            entries[e].checksum = MEM_readLE32((uint8_t*)buf + buf_idx);
            buf_idx += 4;
        }
    }
    entries[num_entries].cOffset = c_offset;
    entries[num_entries].dOffset = d_offset;

    free(buf);
    return true;

fail_w_buf:
    free(buf);
fail:
    return false;
}

ZSTD_seekTable *read_seek_table(FILE *fin)
{
    // TODO: Communicate error info?

    // Save current file position
    long prev_pos = ftell(fin);
    if (prev_pos == -1)
        goto fail;

    // Read seek table footer
    if (fseek(fin, -ZSTD_seekTableFooterSize, SEEK_END) == -1)
        goto fail;
    uint8_t footer[ZSTD_seekTableFooterSize];
    if (fread(footer, 1, ZSTD_seekTableFooterSize, fin) !=
            ZSTD_seekTableFooterSize)
        goto fail_w_seek;
    // Check Seekable_Magic_Number
    if (MEM_readLE32(footer + 5) != ZSTD_SEEKABLE_MAGICNUMBER)
        goto fail_w_seek;
    // Check Seek_Table_Descriptor
    uint8_t std = footer[4];
    if (std & 0x7c)
        // Some of the reserved bits are set
        goto fail_w_seek;
    bool checksum = std & 0x80;
    uint32_t num_frames = MEM_readLE32(footer);

    // Read seek table header
    long seek_entry_size = SEEK_ENTRY_SIZE_NO_CHECKSUM + \
        (checksum ? SEEK_ENTRY_CHECKSUM_SIZE : 0);
    long seek_frame_size = ZSTD_SKIPPABLEHEADERSIZE + \
        num_frames * seek_entry_size + ZSTD_seekTableFooterSize;
    if (fseek(fin, -seek_frame_size, SEEK_END) == -1)
        goto fail_w_seek;
    uint8_t header[ZSTD_SKIPPABLEHEADERSIZE];
    if (fread(header, 1, ZSTD_SKIPPABLEHEADERSIZE, fin) !=
            ZSTD_SKIPPABLEHEADERSIZE)
        goto fail_w_seek;
    // Check Skippable_Magic_Number
    if (MEM_readLE32(header) != SEEKTABLE_SKIPPABLE_MAGICNUMBER)
        goto fail_w_seek;
    // Check Frame_Size
    if (MEM_readLE32(header + 4) != seek_frame_size - ZSTD_SKIPPABLEHEADERSIZE)
        goto fail_w_seek;

    // Read seek table
    seekEntry_t *entries = malloc((num_frames + 1) * sizeof(entries[0]));
    if (!entries)
        goto fail_w_seek;
    if (!read_st_entries(fin, entries, num_frames, checksum))
        goto fail_w_entries;
    ZSTD_seekTable *st = malloc(sizeof(*st));
    if (!st)
        goto fail_w_entries;
    st->entries = entries;
    st->tableLen = num_frames;
    st->checksumFlag = (int)checksum;

    // Restore previous file position
    if (fseek(fin, prev_pos, SEEK_SET) == -1)
        goto fail_w_st;

    return st;

fail_w_st:
    free(st);
fail_w_entries:
    free(entries);
fail_w_seek:
    fseek(fin, prev_pos, SEEK_SET);
fail:
    return NULL;
}

ssize_t offset_to_frame_idx(ZSTD_seekTable* st, size_t offset)
{
    if (offset >= st->entries[st->tableLen].dOffset)
        return -1;

    size_t lo = 0;
    size_t hi = st->tableLen;
    while (lo + 1 < hi) {
        size_t mid = lo + ((hi - lo) / 2);
        if (st->entries[mid].dOffset <= offset)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

off_t frame_offset_c(ZSTD_seekTable* st, size_t frame_idx)
{
    assert(frame_idx < st->tableLen);
    return st->entries[frame_idx].cOffset;
}

off_t frame_offset_d(ZSTD_seekTable* st, size_t frame_idx)
{
    assert(frame_idx < st->tableLen);
    return st->entries[frame_idx].dOffset;
}

size_t frame_size_c(ZSTD_seekTable* st, size_t frame_idx)
{
    assert(frame_idx < st->tableLen);
    return st->entries[frame_idx + 1].cOffset - st->entries[frame_idx].cOffset;
}

size_t frame_size_d(ZSTD_seekTable* st, size_t frame_idx)
{
    assert(frame_idx < st->tableLen);
    return st->entries[frame_idx + 1].dOffset - st->entries[frame_idx].dOffset;
}

/* NOTE: The below are copied verbatim from
zstd/contrib/seekable_format/zstdseek_compress.c @ v1.4.9 */

typedef struct {
    U32 cSize;
    U32 dSize;
    U32 checksum;
} framelogEntry_t;

struct ZSTD_frameLog_s {
    framelogEntry_t* entries;
    U32 size;
    U32 capacity;

    int checksumFlag;

    /* for use when streaming out the seek table */
    U32 seekTablePos;
    U32 seekTableIndex;
} framelog_t;

static size_t ZSTD_seekable_frameLog_allocVec(ZSTD_frameLog* fl)
{
    /* allocate some initial space */
    size_t const FRAMELOG_STARTING_CAPACITY = 16;
    fl->entries = (framelogEntry_t*)malloc(
            sizeof(framelogEntry_t) * FRAMELOG_STARTING_CAPACITY);
    if (fl->entries == NULL) return ERROR(memory_allocation);
    fl->capacity = FRAMELOG_STARTING_CAPACITY;

    return 0;
}

static size_t ZSTD_seekable_frameLog_freeVec(ZSTD_frameLog* fl)
{
    if (fl != NULL) free(fl->entries);
    return 0;
}

ZSTD_frameLog* ZSTD_seekable_createFrameLog(int checksumFlag)
{
    ZSTD_frameLog* fl = malloc(sizeof(ZSTD_frameLog));
    if (fl == NULL) return NULL;

    if (ZSTD_isError(ZSTD_seekable_frameLog_allocVec(fl))) {
        free(fl);
        return NULL;
    }

    fl->checksumFlag = checksumFlag;
    fl->seekTablePos = 0;
    fl->seekTableIndex = 0;
    fl->size = 0;

    return fl;
}

size_t ZSTD_seekable_freeFrameLog(ZSTD_frameLog* fl)
{
    ZSTD_seekable_frameLog_freeVec(fl);
    free(fl);
    return 0;
}

size_t ZSTD_seekable_logFrame(ZSTD_frameLog* fl,
                              unsigned compressedSize,
                              unsigned decompressedSize,
                              unsigned checksum)
{
    if (fl->size == ZSTD_SEEKABLE_MAXFRAMES)
        return ERROR(frameIndex_tooLarge);

    /* grow the buffer if required */
    if (fl->size == fl->capacity) {
        /* exponential size increase for constant amortized runtime */
        size_t const newCapacity = fl->capacity * 2;
        framelogEntry_t* const newEntries = realloc(fl->entries,
                sizeof(framelogEntry_t) * newCapacity);

        if (newEntries == NULL) return ERROR(memory_allocation);

        fl->entries = newEntries;
        assert(newCapacity <= UINT_MAX);
        fl->capacity = (U32)newCapacity;
    }

    fl->entries[fl->size] = (framelogEntry_t){
            compressedSize, decompressedSize, checksum
    };
    fl->size++;

    return 0;
}

static inline size_t ZSTD_seekable_seekTableSize(const ZSTD_frameLog* fl)
{
    size_t const sizePerFrame = 8 + (fl->checksumFlag?4:0);
    size_t const seekTableLen = ZSTD_SKIPPABLEHEADERSIZE +
                                sizePerFrame * fl->size +
                                ZSTD_seekTableFooterSize;

    return seekTableLen;
}

static inline size_t ZSTD_stwrite32(ZSTD_frameLog* fl,
                                    ZSTD_outBuffer* output, U32 const value,
                                    U32 const offset)
{
    if (fl->seekTablePos < offset + 4) {
        BYTE tmp[4]; /* so that we can work with buffers too small to write a whole word to */
        size_t const lenWrite =
                MIN(output->size - output->pos, offset + 4 - fl->seekTablePos);
        MEM_writeLE32(tmp, value);
        memcpy((BYTE*)output->dst + output->pos,
               tmp + (fl->seekTablePos - offset), lenWrite);
        output->pos += lenWrite;
        fl->seekTablePos += lenWrite;

        if (lenWrite < 4) return ZSTD_seekable_seekTableSize(fl) - fl->seekTablePos;
    }
    return 0;
}

size_t ZSTD_seekable_writeSeekTable(ZSTD_frameLog* fl, ZSTD_outBuffer* output)
{
    /* seekTableIndex: the current index in the table and
     * seekTableSize: the amount of the table written so far
     *
     * This function is written this way so that if it has to return early
     * because of a small buffer, it can keep going where it left off.
     */

    size_t const sizePerFrame = 8 + (fl->checksumFlag?4:0);
    size_t const seekTableLen = ZSTD_seekable_seekTableSize(fl);

    CHECK_Z(ZSTD_stwrite32(fl, output, ZSTD_MAGIC_SKIPPABLE_START | 0xE, 0));
    assert(seekTableLen <= (size_t)UINT_MAX);
    CHECK_Z(ZSTD_stwrite32(fl, output, (U32)seekTableLen - ZSTD_SKIPPABLEHEADERSIZE, 4));

    while (fl->seekTableIndex < fl->size) {
        unsigned long long const start = ZSTD_SKIPPABLEHEADERSIZE + sizePerFrame * fl->seekTableIndex;
        assert(start + 8 <= UINT_MAX);
        CHECK_Z(ZSTD_stwrite32(fl, output,
                               fl->entries[fl->seekTableIndex].cSize,
                               (U32)start + 0));

        CHECK_Z(ZSTD_stwrite32(fl, output,
                               fl->entries[fl->seekTableIndex].dSize,
                               (U32)start + 4));

        if (fl->checksumFlag) {
            CHECK_Z(ZSTD_stwrite32(
                    fl, output, fl->entries[fl->seekTableIndex].checksum,
                    (U32)start + 8));
        }

        fl->seekTableIndex++;
    }

    assert(seekTableLen <= UINT_MAX);
    CHECK_Z(ZSTD_stwrite32(fl, output, fl->size,
                           (U32)seekTableLen - ZSTD_seekTableFooterSize));

    if (output->size - output->pos < 1) return seekTableLen - fl->seekTablePos;
    if (fl->seekTablePos < seekTableLen - 4) {
        BYTE sfd = 0;
        sfd |= (fl->checksumFlag) << 7;

        ((BYTE*)output->dst)[output->pos] = sfd;
        output->pos++;
        fl->seekTablePos++;
    }

    CHECK_Z(ZSTD_stwrite32(fl, output, ZSTD_SEEKABLE_MAGICNUMBER,
                           (U32)seekTableLen - 4));

    if (fl->seekTablePos != seekTableLen) return ERROR(GENERIC);
    return 0;
}
