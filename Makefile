phpspy_cflags:=-std=c11 -Wall -Wextra -pedantic -g -O3 -Wno-address-of-packed-member $(CFLAGS)
phpspy_libs:=-pthread $(LDLIBS)
phpspy_ldflags:=$(LDFLAGS)
phpspy_includes:=-I. -I./vendor
phpspy_defines:=
phpspy_tests:=$(wildcard tests/test_*.sh)
phpspy_sources:=addr_objdump.c phpspy.c pyroscope_api.c

termbox_inlcudes=-Ivendor/termbox2/

prefix?=/usr/local

php_path?=php

sinclude config.mk


ifdef USE_ZEND
	$(error USE_ZEND not supported)
endif

ifdef COMMIT
  phpspy_defines:=$(phpspy_defines) -DCOMMIT=$(COMMIT)
endif

all: static

static: $(wildcard *.c *.h) vendor/termbox2/termbox.h
	$(CC) $(phpspy_cflags) $(phpspy_includes) $(termbox_inlcudes) $(phpspy_defines) $(phpspy_sources) -c $(phpspy_ldflags) $(phpspy_libs)
	ar rcs libphpspy.a *.o

vendor/termbox2/termbox.h:
	git submodule update --init --remote --recursive
	cd vendor/termbox2 && git reset --hard

clean:
	rm -f libphpspy.a *.o

.PHONY: all clean static
