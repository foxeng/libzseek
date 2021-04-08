#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>     // size_t

/**
 * Return in @errbuf the equivalent of using perror with @msg and errno set to
 * @errnum.
 * If @errbuf is NULL it simply returns.
 */
void set_error_with_errno(char errbuf[ZSEEK_ERRBUF_SIZE], const char *msg, int errnum);

/**
 * Format a an error message into @errbuf.
 * If @errbuf is NULL it simply returns.
 */
void set_error(char errbuf[ZSEEK_ERRBUF_SIZE], const char *message, ...) __attribute__ ((format(printf, 2, 3)));


#endif  // COMMON_H
