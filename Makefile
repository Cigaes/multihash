CFLAGS += -Wall -W -Wno-pointer-sign -std=c99 -D_XOPEN_SOURCE=700 -fdiagnostics-color=auto
CFLAGS += -g -O2

OBJECTS =
OBJECTS += multihash.o
OBJECTS += cache.o
OBJECTS += formatter.o
OBJECTS += parhash.o
OBJECTS += treewalk.o

multihash: $(OBJECTS)
	$(CC) $(LDFLAGS) -pthread -o $@ $(OBJECTS) -lcrypto -ldb $(LIBS)

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -pthread -c -o $@ $<

multihash.o: cache.h formatter.h parhash.h treewalk.h
cache.o: cache.h
formatter.o: formatter.h
parhash.o: parhash.h
treewalk.o: treewalk.h
