CC = gcc
OPTS = -O2 -g -std=gnu99
DEFINES = -DVGO_linux -DVGA_$(ARCH) -DVGP_$(ARCH)_linux \
	  -DVERSION=\"$(VERSION)\" $(shell pkg-config --cflags valgrind)
WARN = -Wall -Wshadow
CFLAGS = $(OPTS) $(INCLUDES) $(DEFINES) $(WARN)
VERSION := $(shell sed -n 's/Version:[[:space:]]*\(.*\)/\1/p' valgrind-pagein.spec)
ARCH = $(shell pkg-config --variable=arch valgrind)

# XXX valgrind.pc is currently broken.  Hardcode the value
# VALT_LOAD_ADDRESS = $(shell pkg-config --variable=valt_load_address valgrind)
ifeq ($(shell pkg-config --variable=platform valgrind),amd64-linux)
VALT_LOAD_ADDRESS = 0x38000000
else
ifeq ($(shell pkg-config --variable=platform valgrind),x86)
VALT_LOAD_ADDRESS = 0x38000000
else
Architecture not supported
endif
endif

all: pagein

pagein: pg_main.o
	$(CC) -static -nodefaultlibs -nostartfiles -o $@ -u _start \
	      -Wl,--build-id=none,-Ttext=$(VALT_LOAD_ADDRESS) $< \
	      $(shell pkg-config --libs valgrind)

.c.o:
	$(CC) $(CFLAGS) -c $^

.PHONY: dist clean install push pull
dist:
	rm -f valgrind-pagein-$(VERSION)
	ln -s . valgrind-pagein-$(VERSION)
	tar jcvfh valgrind-pagein-$(VERSION).tar.bz2 valgrind-pagein-$(VERSION)/{Makefile,pg_main.c,valgrind-pagein.spec}
	rm -f valgrind-pagein-$(VERSION)

clean:
	rm -f pagein pg_main.o valt_load_address.lds

push:
	git push
pull:
	git pull

install: all
	install -cDs pagein $(DESTDIR)$(shell pkg-config --variable=libdir valgrind)/valgrind/pagein-$(shell pkg-config --variable=platform valgrind)
