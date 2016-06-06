#
# For out-of-tree builds:
# make -f /path/to/src/Makefile CFLAGS=... configure
#

ifeq ($(srcdir),)

  srcdir = ./
  CFLAGS += -O2 -std=c99 -D_XOPEN_SOURCE=700
  CFLAGS += -Wall -W -Wno-pointer-sign -fdiagnostics-color=auto -g

endif

OBJECTS =
OBJECTS += multihash.o
OBJECTS += cache.o
OBJECTS += formatter.o
OBJECTS += parhash.o
OBJECTS += treewalk.o

multihash: $(OBJECTS)
	$(CC) $(LDFLAGS) -pthread -o $@ $(OBJECTS) -lcrypto -ldb $(LIBS)

$(OBJECTS): %.o: $(srcdir)%.c
	$(CC) $(CFLAGS) -pthread -c -o $@ $<

multihash.o cache.o: $(srcdir)cache.h
multihash.o formatter.o: $(srcdir)formatter.h
multihash.o parhash.o: $(srcdir)parhash.h
multihash.o treewalk.o: $(srcdir)treewalk.h

.PHONY: configure clean

configure:
	@if [ -e Makefile ]; then echo "Makefile is in the way"; false; fi
	@{ \
	  printf "srcdir = %s\n" $(dir $(MAKEFILE_LIST)) ; \
	  printf "CFLAGS = %s\n" "$(CFLAGS)" ; \
	  printf "LDFLAGS = %s\n" "$(LDFLAGS)" ; \
	  printf "LIBS = %s\n" "$(LIBS)" ; \
	  printf "include \$$(srcdir)Makefile\n" ; \
	} > Makefile

clean:
	-rm -f multihash $(OBJECTS)
