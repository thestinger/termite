PREFIX = /usr/local
GTK = gtk+-3.0
VTE = vte-2.90

CFLAGS += -std=c99 -O3 \
	  -Wall -Wextra -pedantic \
	  -Winit-self \
	  -Wshadow \
	  -Wformat=2 \
	  -Wmissing-declarations \
	  -Wstrict-overflow=5 \
	  -Wcast-align \
	  -Wcast-qual \
	  -Wconversion \
	  -Wc++-compat \
	  -Wbad-function-cast \
	  -Wunused-macros \
	  $(shell pkg-config --cflags ${GTK} ${VTE})

ifeq (${CC}, clang)
	CFLAGS += -Wno-missing-field-initializers
endif

LDFLAGS += -s -Wl,--as-needed $(shell pkg-config --libs ${GTK} ${VTE})

termite: termite.c config.h
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS} -g

install: termite
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f termite ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/termite

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/termite

.PHONY: install uninstall
