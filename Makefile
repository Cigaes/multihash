CFLAGS += -Wall -W -Wno-pointer-sign -std=c99 -D_XOPEN_SOURCE=600 -fdiagnostics-color=auto
CFLAGS += -g -O2

OBJECTS =
OBJECTS += cahash.o
OBJECTS += parhash.o

cahash: $(OBJECTS)
	$(CC) $(LDFLAGS) -pthread -o $@ $(OBJECTS) -lcrypto -ldb $(LIBS)

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -pthread -c -o $@ $<

cahash.o: parhash.h
parhash.o: parhash.h
