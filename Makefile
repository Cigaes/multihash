#
# For out-of-tree builds:
# make -f /path/to/src/Makefile CFLAGS=... configure
#

ifeq ($(srcdir),)

  srcdir = $(dir $(MAKEFILE_LIST))
  CFLAGS += -O2 -std=c99 -D_XOPEN_SOURCE=700
  CFLAGS += -Wall -W -Wno-pointer-sign -fdiagnostics-color=auto -g
  PREFIX ?= /usr/local

endif

# Set configuration varialbes here if needed

#CFLAGS += -I/opt/openssl/include
#LDFLAGS += -L/opt/openssl/lib
#LIBS =
#PREFIX = /opt/multihash

OBJECTS =
OBJECTS += multihash.o
OBJECTS += cache.o
OBJECTS += formatter.o
OBJECTS += parhash.o
OBJECTS += treewalk.o
OBJECTS += archive.o

multihash: $(OBJECTS)
	$(CC) $(LDFLAGS) -pthread -o $@ $(OBJECTS) -lcrypto -ldb $(LIBS)

$(OBJECTS): %.o: $(srcdir)%.c
	$(CC) $(CFLAGS) $(CFLAGS_SRC) -pthread -c -o $@ $<

multihash.o cache.o: $(srcdir)cache.h
multihash.o formatter.o: $(srcdir)formatter.h
multihash.o parhash.o: $(srcdir)parhash.h
multihash.o treewalk.o: $(srcdir)treewalk.h
multihash.o archive.o: $(srcdir)archive.h

VERSION = $$(git --git-dir $(srcdir)/.git log -n 1 --date=format:%Y%m%d --format=%ad-%h)
multihash.o: CFLAGS_SRC += -DVERSION=\"$(VERSION)\"
multihash.o: $(srcdir).git/logs/HEAD

.PHONY: configure install clean

configure:
	@if [ -e Makefile ]; then echo "Makefile is in the way"; false; fi
	@{ \
	  printf "srcdir = %s\n" $(srcdir) ; \
	  printf "CFLAGS = %s\n" "$(CFLAGS)" ; \
	  printf "LDFLAGS = %s\n" "$(LDFLAGS)" ; \
	  printf "LIBS = %s\n" "$(LIBS)" ; \
	  printf "PREFIX = %s\n" "$(PREFIX)" ; \
	  printf "include \$$(srcdir)Makefile\n" ; \
	} > Makefile

test: multihash
	rm -rf tests
	perl $(srcdir)selftest.plx

install: multihash
	install -D -m 755 multihash $(DESTDIR)$(PREFIX)/bin/multihash
	install -D -m 644 $(srcdir)multihash.1 $(DESTDIR)$(PREFIX)/share/man/man1/multihash.1

clean:
	-rm -f multihash $(OBJECTS)
