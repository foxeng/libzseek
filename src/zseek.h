/**
 * @defgroup libzseek Efficient compressed file abstraction
 *
 * Efficient sequential write and random access read API using ZSTD.
 *
 * The file must be written out sequentially in one go,
 * but can be opened for random reads using offsets and sizes
 * of decompressed data, as if the file was not compressed.
 *
 * Internally, an index from uncompressed to compressed file offsets is used
 * to map decompressed offsets to the enclosing compressed block.
 * This index is appended to the file after closing the write handle,
 * and consulted for serving random access reads.
 *
 * The block sizes are tuned to achieve high throughput write without
 * prohibitive read amplification.
 *
 * @{
 */

#ifndef ZSEEK_H
#define ZSEEK_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include <sys/types.h>
#include <sched.h>

/**
 * Error buffer size
 */
#define ZSEEK_ERRBUF_SIZE 80

/**
 * Pluggable write handler
 *
 * @param data
 *  The data to write to the file
 * @param size
 *  The size of @p data
 * @param user_data
 *  The user-specified file handle
 *
 * @retval true
 *  On success. @p size bytes from @p data written to the file.
 * @retval false
 *  On error
 */
typedef bool (*zseek_write_t)(const void *data, size_t size, void *user_data);

/**
 * User-defined file supporting writes
 */
typedef struct {
    /** File handle to use when calling below functions */
    void *user_data;
    /** Write function */
    zseek_write_t write;
} zseek_write_file_t;

/**
 * Pluggable read handler
 *
 * @param[out] data
 *  The destination for the data read
 * @param size
 *  The number of bytes to read
 * @param offset
 *  The file offset to read from
 * @param user_data
 *  The user-specified file handle
 *
 * @retval N>=0
 *  Number of bytes read and stored in @p data. May be less than @p size if EOF
 *  was encountered.
 * @retval <0
 *  On error
 */
typedef ssize_t (*zseek_pread_t)(void *data, size_t size, size_t offset,
    void *user_data);

/**
 * Pluggable file size handler
 *
 * @param user_data
 *  The user-specified file handle
 *
 * @retval N>=0
 *  File size, in bytes
 * @retval <0
 *  On error
 */
typedef ssize_t (*zseek_fsize_t)(void *user_data);

/**
 * User-defined file supporting reads
 */
typedef struct {
    /** File handle to use when calling below functions */
    void *user_data;
    /** Read function */
    zseek_pread_t pread;
    /** File size function */
    zseek_fsize_t fsize;
} zseek_read_file_t;

/**
 * Supported compression algorithms
 */
typedef enum {
    ZSEEK_ZSTD = 0
} zseek_compression_type_t;

/**
 * Compression and multi-threading controls for zstd
 */
typedef struct {
    /** Number of worker threads */
    int nb_workers;
    /** The size of @ref cpuset. See pthread_setaffinity_np (3) */
    size_t cpusetsize;
    /** The CPU set to confine the workers to. See pthread_setaffinity_np (3) */
    cpu_set_t *cpuset;
    /** Compression level (default = 3) */
    int compression_level;
    /** Compression strategy (default = fast)  */
    int strategy;
} zseek_zstd_param_t;

/**
 * Collection of algorithm specific control options
 */
typedef struct {
    zseek_compression_type_t type;
    union {
        zseek_zstd_param_t zstd_params;
    } params;
} zseek_compression_param_t;

/**
 * Handle to a compressed file for sequential writes
 */
typedef struct zseek_writer zseek_writer_t;

/**
 * Handle to a compressed file for random access reads
 */
typedef struct zseek_reader zseek_reader_t;

/**
 * Collection of writer statistics
 */
typedef struct {
    /** Size of seek table in bytes (on disk) */
    size_t seek_table_size;
    /** Memory usage of seek table in bytes */
    size_t seek_table_memory;
    /** Number of frames */
    size_t frames;
    /** Estimate for compressed data size in bytes. Always <= actual size. */
    size_t compressed_size;
} zseek_writer_stats_t;

/**
 * Collection of reader statistics
 */
typedef struct {
    /** Memory usage of seek table in bytes */
    size_t seek_table_memory;
    /** Number of frames */
    size_t frames;
    /** Decompressed file size in bytes */
    size_t decompressed_size;
    /** Memory usage of reader cache in bytes */
    size_t cache_memory;
    /** Number of frames currently cached */
    size_t cached_frames;
} zseek_reader_stats_t;

/**
 * Creates a compressed file for sequential writes
 *
 * @param user_file
 *	File to write compressed data to
 * @param zsp
 *	Compression tunables and multi-threading controls.
 *	If @a NULL defaults are applied
 * @param min_frame_size
 *	Minimum (uncompressed) frame size
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval writer
 *  Handle to perform writes
 * @retval NULL
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
zseek_writer_t *zseek_writer_open_full(zseek_write_file_t user_file,
    zseek_compression_param_t *zsp, size_t min_frame_size,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Creates a compressed file for sequential writes, with default file I/O
 *
 * @param cfile
 *	File to write compressed data to
 * @param zsp
 *	Compression tunables and multi-threading controls.
 *	If @a NULL defaults are applied
 * @param min_frame_size
 *	Minimum (uncompressed) frame size
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval writer
 *  Handle to perform writes
 * @retval NULL
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
zseek_writer_t *zseek_writer_open(FILE *cfile, zseek_compression_param_t *zsp,
    size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Closes a compressed file handle for writes
 *
 * @param writer
 *	Compressed file handle to close
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval true
 *  On success. The @p reader is de-allocated and no longer usable.
 * @retval false
 *  On error. If not @a NULL, @p errbuf is populated with an error message. The
 *  @p reader is de-allocated and no longer usable.
 */
bool zseek_writer_close(zseek_writer_t *writer, char errbuf[ZSEEK_ERRBUF_SIZE]);


/**
 * Appends data to a compressed file
 *
 * The data chunks passed can be small, they will be coalesced
 * internally for efficient compression and IO.
 *
 * This is \e not safe to call concurrently. It will not, in general, return
 * immediately.
 *
 * @param writer
 *	Compressed file write handle
 * @param buf
 *	Pointer to data to write
 * @param len
 *	Length of data pointed to by @p buf
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval true
 *  On success
 * @retval false
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
bool zseek_write(zseek_writer_t *writer, const void *buf, size_t len,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Returns currently available writer statistics
 *
 * @param writer
 *	Compressed file handle to get stats for
 * @param[out] stats
 *  Pointer to stats structure to populate
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval true
 *  On success
 * @retval false
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
bool zseek_writer_stats(zseek_writer_t *writer, zseek_writer_stats_t *stats,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Creates a reader for random access reads
 *
 * @param user_file
 *  File to read compressed data from
 * @param cache_size
 *  Maximum number of decompressed frames to cache
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval reader
 *  Handle to perform reads
 * @retval NULL
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
zseek_reader_t *zseek_reader_open_full(zseek_read_file_t user_file,
    size_t cache_size, char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Creates a reader for random access reads, with default file I/O
 *
 * @param cfile
 *  File to read compressed data from
 * @param cache_size
 *  Size of decompressed frames to cache
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval reader
 *  Handle to perform reads
 * @retval NULL
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
zseek_reader_t *zseek_reader_open(FILE *cfile, size_t cache_size,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Closes a compressed file handle for reads
 *
 * @param reader
 *	The compressed file reader to close
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval true
 *  On success. The @p reader is de-allocated and no longer usable.
 * @retval false
 *	On error. If not @a NULL, @p errbuf is populated with an error message. The
 *  @p reader is de-allocated and no longer usable.
 */
bool zseek_reader_close(zseek_reader_t *reader, char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Reads data from an arbitrary offset of a compressed file
 *
 * @param reader
 *	Compressed file reader
 * @param[out] buf
 *	Buffer to store decompressed data
 * @param count
 *	Size of decompressed data to read
 * @param offset
 *	Offset in the decompressed data to read data from
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval N>=0
 *	Number of bytes read
 * @retval -1
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
ssize_t zseek_pread(zseek_reader_t *reader, void *buf, size_t count,
    size_t offset, char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Reads data from the current offset of a compressed file
 *
 * @param reader
 *	Compressed file reader
 * @param[out] buf
 *	Buffer to store decompressed data
 * @param count
 *	Size of decompressed data to read
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval N
 *  Number of bytes read
 * @retval -1
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
ssize_t zseek_read(zseek_reader_t *reader, void *buf, size_t count,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Returns currently available reader statistics
 *
 * @param reader
 *	Compressed file handle to get stats for
 * @param[out] stats
 *  Pointer to stats structure to populate
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 *
 * @retval true
 *  On success
 * @retval false
 *  On error. If not @a NULL, @p errbuf is populated with an error message.
 */
bool zseek_reader_stats(zseek_reader_t *reader, zseek_reader_stats_t *stats,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

#endif

/**
 * @}
 */
