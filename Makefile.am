
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = libzseek.pc \
	     README.md \
	     $(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzseek.pc

AM_CFLAGS = $(WARN_CFLAGS) $(PTHREAD_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/src -include config.h

lib_LTLIBRARIES = libzseek.la

libzseek_la_LDFLAGS = $(WARN_LDFLAGS) -version-info $(LIBZSEEK_CURRENT):$(LIBZSEEK_REVISION):$(LIBZSEEK_AGE)

libzseek_la_CFLAGS = $(AM_CFLAGS) $(ZSTD_CFLAGS) $(LZ4_CFLAGS)
libzseek_la_CPPFLAGS = $(AM_CPPFLAGS)
libzseek_la_LIBADD = $(ZSTD_LIBS) $(LZ4_LIBS) $(PTHREAD_LIBS)

libzseek_la_SOURCES = src/seek_table.h \
		      src/compress.c \
		      src/decompress.c \
		      src/seek_table.c \
		      src/common.h \
		      src/common.c \
			  src/cache.h \
			  src/cache.c \
			  src/buffer.h \
			  src/buffer.c

include_HEADERS = src/zseek.h

noinst_PROGRAMS = benchmark example test_cache test_buffer

benchmark_SOURCES = test/benchmark.c $(HEADERS)
benchmark_LDADD = $(ZSTD_LIBS) $(LZ4_LIBS) -lm $(top_builddir)/libzseek.la

example_SOURCES = test/example.c $(HEADERS)
example_LDADD = $(ZSTD_LIBS) $(LZ4_LIBS) -lm $(top_builddir)/libzseek.la

test_cache_SOURCES = test/test_cache.c $(top_builddir)/src/cache.h
test_cache_CFLAGS = @CHECK_CFLAGS@
test_cache_LDADD = -lm $(top_builddir)/libzseek.la @CHECK_LIBS@

test_buffer_SOURCES = test/test_buffer.c $(top_builddir)/src/buffer.h
test_buffer_CFLAGS = @CHECK_CFLAGS@
test_buffer_LDADD = -lm $(top_builddir)/libzseek.la @CHECK_LIBS@
