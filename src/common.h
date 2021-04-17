#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>     // size_t

/**
 * Return in @p errbuf the equivalent of using perror with @p msg and errno set
 * to @p errnum.
 * If @p errbuf is NULL it simply returns.
 */
void set_error_with_errno(char errbuf[ZSEEK_ERRBUF_SIZE], const char *msg, int errnum);

/**
 * Format an error message into @p errbuf.
 * If @p errbuf is NULL it simply returns.
 */
void set_error(char errbuf[ZSEEK_ERRBUF_SIZE], const char *message, ...) __attribute__ ((format(printf, 2, 3)));


#endif  // COMMON_H
