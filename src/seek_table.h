/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#ifndef SEEK_TABLE_H
#define SEEK_TABLE_H

#include <stddef.h>     // size_t
#include <sys/types.h>  // off_t

#include "zseek.h"

typedef struct ZSTD_frameLog_s ZSTD_frameLog;
typedef struct ZSTD_seekTable_s ZSTD_seekTable;
ZSTD_frameLog* ZSTD_seekable_createFrameLog(int checksumFlag);
size_t ZSTD_seekable_freeFrameLog(ZSTD_frameLog* fl);
size_t ZSTD_seekable_logFrame(ZSTD_frameLog* fl, unsigned compressedSize,
    unsigned decompressedSize, unsigned checksum);
size_t ZSTD_seekable_writeSeekTable(ZSTD_frameLog* fl, ZSTD_outBuffer* output);

/**
 * Parse and return the seek table found in the last frame contained in @p fin,
 * or NULL on error.
 */
ZSTD_seekTable *read_seek_table(zseek_read_file_t user_file);
/**
 * Free the seek table pointed to by @p st.
 */
void seek_table_free(ZSTD_seekTable *st);
/**
 * Return the index of the frame containing decompressed @p offset or -1 if
 * offset is out of range.
 */
ssize_t offset_to_frame_idx(ZSTD_seekTable *st, size_t offset);
/**
 * Return the offset in the compressed file of the frame at index @p frame_idx.
 */
off_t frame_offset_c(ZSTD_seekTable *st, size_t frame_idx);
/**
 * Return the offset in the decompressed file of the frame at index
 * @p frame_idx.
 */
off_t frame_offset_d(ZSTD_seekTable *st, size_t frame_idx);
/**
 * Return the size of the compressed frame at index @p frame_idx.
 */
size_t frame_size_c(ZSTD_seekTable *st, size_t frame_idx);
/**
 * Return the size of the decompressed frame at index @p frame_idx.
 */
size_t frame_size_d(ZSTD_seekTable *st, size_t frame_idx);

#endif /* SEEK_TABLE_H */
