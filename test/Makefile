ZSTD_VERSION = v1.4.9
LIBZSTD_DIR = zstd/lib
LIBZSTD_FILE = libzstd.a
LIBZSTD = $(LIBZSTD_DIR)/$(LIBZSTD_FILE)
LIBZSEEK_DIR = ..
LIBZSEEK_FILE = libzseek.a
LIBZSEEK = $(LIBZSEEK_DIR)/$(LIBZSEEK_FILE)

CFLAGS += -Wall -std=c99 -pthread -O3 -g
LDFLAGS += -pthread -L$(LIBZSTD_DIR)
LDLIBS += -lm -l:$(LIBZSTD_FILE)	# Necessary, because -lzstd prefers the .so over the .a


.PHONY: all clean distclean

all: test benchmark

zstd:
	git clone --depth 1 --single-branch -b $(ZSTD_VERSION) https://github.com/facebook/zstd.git

$(LIBZSTD): zstd
	$(MAKE) -C $(LIBZSTD_DIR) lib-mt

test: $(LIBZSTD) $(LIBZSEEK) test.o

benchmark: $(LIBZSTD) $(LIBZSEEK) benchmark.o


clean:
	$(RM) test benchmark *.o

distclean: clean
	$(RM) -r zstd
