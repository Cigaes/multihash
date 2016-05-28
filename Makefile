CFLAGS += -Wall -W -Wno-pointer-sign -std=c99 -D_XOPEN_SOURCE=600 -fdiagnostics-color=auto
CFLAGS += -g -O2

OBJECTS =
OBJECTS += multihash.o
OBJECTS += cache.o
OBJECTS += parhash.o

multihash: $(OBJECTS)
	$(CC) $(LDFLAGS) -pthread -o $@ $(OBJECTS) -lcrypto -ldb $(LIBS)

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -pthread -c -o $@ $<

multihash.o: cache.h parhash.h
cache.o: cache.h
parhash.o: parhash.h
