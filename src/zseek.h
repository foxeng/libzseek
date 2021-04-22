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
 * Additional considerations:
 *
 * An initial segment of the file is uncompressed,
 * and can be used to store metadata by overwriting in-place by the caller
 * after the compression is concluded.
 * This is represented by the start_offset parameter.
 * Separately, we are looking into changing the potential users to append
 * their metadata to the end of the file,
 * so that we can eliminate this feature.
 *
 * Should have a pluggable downstream writer and reader,
 * instead of doing directly IO on the file, because we
 * have a custom Linux specific high performance one that looks like
 * this:
 *  bool buffered_write(const void *data, size_t size, *user_data)
 *  bool buffered_read(void *data, size_t size, *user_data)
 *
 * @{
 */

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/**
 * Error buffer size
 */
#define ZSEEK_ERRBUF_SIZE 80

/**
 * Pluggable write handler
 *
 * @param data
 * @param size
 * @param user_data
 * @todo add a setter for this, or pass in constructor
 */
// typedef bool (*zseek_write_t)(const void *data, size_t size, *user_data);

/**
 * Pluggable read handler
 *
 * @param[out] data
 * @param size
 * @param offset
 * @param user_data
 * @todo add a setter for this, or pass in constructor
 */
// typedef ssize_t (*zseek_pread_t)(void *data, size_t size, size_t offset,
//     *user_data);

/**
 * Handle to a compressed file for sequential writes
 */
typedef struct zseek_writer zseek_writer_t;

/**
 * Handle to a compressed file for random access reads
 */
typedef struct zseek_reader zseek_reader_t;

/**
 * Creates a compressed file for sequential writes
 *
 * @param filename
 *	Name of the file to create. The file must not exist
 * @param nb_workers
 *	Number of worker threads to use for compression
 * @param min_frame_size
 *	Minimum (uncompressed) frame size
 * @param[out] errbuf
 *
 * @return
 *	Handle to perform writes or @a NULL on error
 */
zseek_writer_t *zseek_writer_open(const char *filename, int nb_workers,
    size_t min_frame_size, char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Closes a compressed file handle for writes
 *
 * @param writer
 *	Compressed file handle to close
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 * @return
 *	True on success, false on error. If not @a NULL, @p errbuf is
 *	populated with an error message. In either case,
 *	the @p reader is de-allocated and no longer usable.
 */
bool zseek_writer_close(zseek_writer_t *writer, char errbuf[ZSEEK_ERRBUF_SIZE]);


/**
 * Appends data to a compressed file
 *
 * The data chunks passed can be small, they will be coalesced
 * internally for efficient compression and IO.
 *
 * This is safe to call concurrently. It will not, in general, return
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
 * @return
 *	True on success, false on error. If not @a NULL, @p errbuf is
 *	populated with an error message.
 */
bool zseek_write(zseek_writer_t *writer, const void *buf, size_t len,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * @param filename
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 * @return
 *	True on success, false on error. If not @a NULL, @p errbuf is
 *	populated with an error message.
 */
zseek_reader_t *zseek_reader_open(const char *filename,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * Closes a compressed file handle for reads
 *
 * @param reader
 *	The compressed file reader to close
 * @param[out] errbuf
 *	Pointer to error message buffer or @a NULL
 * @return
 *	True on success, false on error. If not @a NULL, @p errbuf is
 *	populated with an error message. In either case,
 *	the @p reader is de-allocated and no longer usable.
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
 *
 * @param[out] errbuf
 * @return
 *	Number of bytes read, -1 on error. If not @a NULL, @p errbuf is
 *	populated with an error message.
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
 *
 * @param[out] errbuf
 * @return
 *	Number of bytes read, -1 on error. If not @a NULL, @p errbuf is
 *	populated with an error message.
 */
ssize_t zseek_read(zseek_reader_t *reader, void *buf, size_t count,
    char errbuf[ZSEEK_ERRBUF_SIZE]);

/**
 * @}
 */
