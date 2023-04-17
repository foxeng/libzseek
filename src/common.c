#include <string.h>     // strerror_r, memset
#include <stdio.h>      // snprintf
#include <stdlib.h>     // malloc, realloc, free
#include <stdarg.h>

#include "zseek.h"      // ZSEEK_ERRBUF_SIZE

#include "common.h"

// Portable strerror_r
static const char *xstrerror_r(int errnum, char *buf, size_t buflen)
{
    const char *errstr;

#if (_POSIX_C_SOURCE >= 200112L) && !(defined(_GNU_SOURCE) && _GNU_SOURCE)
    // XSI-compliant
    if (strerror_r(errnum, buf, buflen) == 0)
        errstr = buf;
    else
        errstr = "";
    // Assume buf is always NULL-terminated (not mentioned in the docs)
#else
    // GNU-specific
    errstr = strerror_r(errnum, buf, buflen);
#endif

    return errstr;
}

void set_error_with_errno(char errbuf[ZSEEK_ERRBUF_SIZE], const char *msg, int errnum)
{
    // "The GNU C Library uses a buffer of 1024 characters for strerror(). This
    // buffer size therefore should be sufficient to avoid an ERANGE error when
    // calling strerror_r()."
    char buf2[1024];

    const char *errstr = xstrerror_r(errnum, buf2, 1024);

    if (!msg || *msg == '\0')
        set_error(errbuf, "%s", errstr);
    else
        set_error(errbuf, "%s: %s", msg, errstr);
}


void set_error(char errbuf[ZSEEK_ERRBUF_SIZE], const char *message, ...)
{
    if (!errbuf)
        return;

    va_list arg_ptr;
    va_start(arg_ptr, message);
    vsnprintf(errbuf, ZSEEK_ERRBUF_SIZE, message, arg_ptr);
    va_end(arg_ptr);
}

static void *default_malloc(size_t size, bool zero, void *user_data)
{
    (void)user_data;

    void *ptr = malloc(size);
    if (!ptr)
        return NULL;

    // TODO OPT: Use calloc for zero-init?
    if (zero)
        memset(ptr, 0, size);

    return ptr;
}

static void *default_realloc(void *ptr, size_t size, void *user_data)
{
    (void)user_data;

    return realloc(ptr, size);
}

static void default_free(void *ptr, void *user_data)
{
    (void)user_data;

    free(ptr);
}

void init_mm(zseek_mm_t *mm)
{
    if (!mm->malloc)
        mm->malloc = default_malloc;

    if (!mm->realloc)
        mm->realloc = default_realloc;

    if (!mm->free)
        mm->free = default_free;
}
