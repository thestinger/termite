PREFIX = /usr/local

CFLAGS += -std=c99 -O3 \
	  -Wall -Wextra -pedantic \
	  -Winit-self \
	  -Wshadow \
	  -Wformat=2 \
	  -Wmissing-declarations \
	  $(shell pkg-config --cflags gtk+-2.0 vte)

LDFLAGS += -s -Wl,--as-needed $(shell pkg-config --libs gtk+-2.0 vte)

termite: termite.c config.h
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS}

install: termite
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f termite ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/termite

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/termite

.PHONY: install uninstall
