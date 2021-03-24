LIB_SHARED = libnioarchive.so
LIB_STATIC = libnioarchive.a
objs = seek_table.o compress.o decompress.o

CFLAGS += -Wall -std=gnu99 -pthread -O3 -g
LDFLAGS += -pthread
ARFLAGS = rcs


.PHONY: all clean

all: $(LIB_STATIC)

$(LIB_STATIC): $(LIB_STATIC)($(objs))

# TODO: Build .so too. Need to namespace the object files.
# $(LIB_SHARED): CFLAGS += -fPIC
# $(LIB_SHARED): LDFLAGS += -shared
# $(LIB_SHARED): $(objs)
# 	$(CC) $(LDFLAGS) $^ -o $@


clean:
	$(RM) *.o $(LIB_SHARED) $(LIB_STATIC)
