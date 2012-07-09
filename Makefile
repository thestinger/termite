VERSION = $(shell git describe --tags)
PREFIX = /usr/local
GTK = gtk+-3.0
VTE = vte-2.90
TERMINFO = ${PREFIX}/share/terminfo

CFLAGS := -std=c99 -O3 \
	  -Wall -Wextra -pedantic \
	  -Winit-self \
	  -Wshadow \
	  -Wformat=2 \
	  -Wmissing-declarations \
	  -Wstrict-overflow=4 \
	  -Wcast-align \
	  -Wcast-qual \
	  -Wconversion \
	  -Wc++-compat \
	  -Wbad-function-cast \
	  -Wunused-macros \
	  -Wwrite-strings \
	  -DTERMITE_VERSION=\"${VERSION}\" \
	  ${shell pkg-config --cflags ${GTK} ${VTE}} \
	  ${CFLAGS}

ifeq (${CC}, clang)
	CFLAGS += -Wno-missing-field-initializers
endif

LDFLAGS := -s -Wl,--as-needed ${shell pkg-config --libs ${GTK} ${VTE}} ${LDFLAGS}

termite: termite.c
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS}

install: termite
	mkdir -p ${DESTDIR}${TERMINFO}
	install -Dm755 termite ${DESTDIR}${PREFIX}/bin/termite
	tic termite.terminfo -o ${DESTDIR}${TERMINFO}
	install -Dm644 termite.vim ${DESTDIR}${PREFIX}/share/vim/vimfiles/plugin/termite.vim

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/termite

clean:
	rm termite

.PHONY: clean install uninstall
