PREFIX = /usr/local

ifeq (${GTK3}, 1)
	GTK = gtk+-3.0
	VTE = vte-2.90
else
	GTK = gtk+-2.0
	VTE = vte
endif

CFLAGS += -std=c99 -O3 \
	  -Wall -Wextra -pedantic \
	  -Winit-self \
	  -Wshadow \
	  -Wformat=2 \
	  -Wmissing-declarations \
	  $(shell pkg-config --cflags ${GTK} ${VTE})

LDFLAGS += -s -Wl,--as-needed $(shell pkg-config --libs ${GTK} ${VTE})

termite: termite.c config.h
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS}

install: termite
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f termite ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/termite

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/termite

.PHONY: install uninstall
