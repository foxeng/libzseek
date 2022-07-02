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
ZSTD_seekTable *read_seek_table(zseek_read_file_t user_file, void *call_data);
/**
 * Free the seek table pointed to by @p st.
 */
void seek_table_free(ZSTD_seekTable *st);
/**
 * Return the index of the chunk containing decompressed @p offset or -1 if
 * offset is out of range.
 */
ssize_t offset_to_chunk_idx(ZSTD_seekTable *st, size_t offset);
/**
 * Return the offset in the compressed file of the chunk at index @p chunk_idx.
 */
off_t chunk_offset_c(ZSTD_seekTable *st, size_t chunk_idx);
/**
 * Return the offset in the decompressed file of the chunk at index
 * @p chunk_idx.
 */
off_t chunk_offset_d(ZSTD_seekTable *st, size_t chunk_idx);
/**
 * Return the size of the compressed chunk at index @p chunk_idx.
 */
size_t chunk_size_c(ZSTD_seekTable *st, size_t chunk_idx);
/**
 * Return the size of the decompressed chunk at index @p chunk_idx.
 */
size_t chunk_size_d(ZSTD_seekTable *st, size_t chunk_idx);

/**
 * Return the size in bytes that @p fl would take up if written to disk.
 */
size_t framelog_size(const ZSTD_frameLog *fl);
/**
 * Return the memory usage (total heap allocation) of @p fl in bytes.
 */
size_t framelog_memory_usage(const ZSTD_frameLog *fl);
/**
 * Return the number of entries in @p fl.
 */
size_t framelog_entries(const ZSTD_frameLog *fl);

/**
 * Return the memory usage (total heap allocation) of @p st in bytes.
 */
size_t seek_table_memory_usage(const ZSTD_seekTable *st);
/**
 * Return the number of entries in @p st.
 */
size_t seek_table_entries(const ZSTD_seekTable *st);
/**
 * Return the total decompressed size of the frames in @p st.
 */
size_t seek_table_decompressed_size(const ZSTD_seekTable *st);

#endif /* SEEK_TABLE_H */
