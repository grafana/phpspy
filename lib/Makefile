phpspy_cflags:=-std=gnu11 -Wall -Werror -Wextra -pedantic -O3 -Wno-unused $(CFLAGS)
phpspy_cppflags:=-std=c++11 -Wall -Wextra -pedantic -O3 $(CFLAGS) $(CPPFLAGS)
phpspy_libs:=-pthread $(LDLIBS)
phpspy_ldflags:=$(LDFLAGS)
phpspy_includes:=-I.
phpspy_defines:=
phpspy_tests:=$(wildcard tests/test_*.sh)
phpspy_sources:=phpspy.c addr_objdump.c pyroscope_api.c phpspy_trace.c

prefix?=/usr/local

php_path?=php

sinclude config.mk

phpspy_cflags:=$(subst c90,c11,$(phpspy_cflags))
phpspy_includes:=$(phpspy_includes) $$(php-config --includes)

all: static

tests: static
	$(CXX) $(phpspy_cppflags) $(phpspy_includes) $(termbox_includes) \
	$(phpspy_defines) $(phpspy_ldflags) $(termbox_libs) ./tests/pyroscope_api/*.cpp -lstdc++ \
	-I /usr/src/gtest -L /usr/local/lib/ -lgtest $(phpspy_libs) \
	 ./gtest_main.cpp libphpspy.a \
	-o pyroscope_api_tests

static: $(wildcard *.c *.h)
	$(CC) $(phpspy_cflags) $(phpspy_includes) $(phpspy_defines) $(phpspy_sources) -c $(phpspy_ldflags) $(phpspy_libs) -fPIC
	ar rcs libphpspy.a *.o

dynamic: $(wildcard *.c *.h)
	$(CC) $(phpspy_cflags) $(phpspy_includes) $(phpspy_defines) $(phpspy_sources) $(phpspy_ldflags) $(phpspy_libs) -fPIC -shared -o libphpspy.so

clean:
	rm -f ./*.a ./*.so ./*.o pyroscope_api_tests

.PHONY: all tests clean static dynamic
