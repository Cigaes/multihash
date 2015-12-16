CFLAGS += -Wall -W -Wno-pointer-sign -std=c99 -D_XOPEN_SOURCE=600 -fdiagnostics-color=auto
CFLAGS += -g -O2

cahash: cahash.c
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $< -lcrypto -ldb
