PREFIX = /usr/local

CC = gcc
CFLAGS = -std=c99 -O3 \
	 -Wall -Wextra -pedantic \
	 -Winit-self \
	 -Wshadow \
	 -Wformat=2 \
	 -Wmissing-declarations

CFLAGS += $(shell pkg-config --cflags gtk+-2.0 vte)
LDFLAGS += -s -Wl,--as-needed $(shell pkg-config --libs gtk+-2.0 vte)

term: term.c config.h
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS}

install: term
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f term ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/term

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/term

.PHONY: install uninstall
