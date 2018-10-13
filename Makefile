VERSION = $(shell git describe --tags)
GTK = gtk+-3.0
VTE = vte-2.91
PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
DATADIR ?= ${PREFIX}/share
MANDIR ?= ${DATADIR}/man
TERMINFO ?= ${DATADIR}/terminfo

CXXFLAGS := -std=c++11 -O3 \
	    -Wall -Wextra -pedantic \
	    -Winit-self \
	    -Wshadow \
	    -Wformat=2 \
	    -Wmissing-declarations \
	    -Wstrict-overflow=5 \
	    -Wcast-align \
	    -Wconversion \
	    -Wunused-macros \
	    -Wwrite-strings \
	    -DNDEBUG \
	    -D_POSIX_C_SOURCE=200809L \
	    -DTERMITE_VERSION=\"${VERSION}\" \
	    ${shell pkg-config --cflags ${GTK} ${VTE}} \
	    ${CXXFLAGS}

ifeq (${CXX}, g++)
	CXXFLAGS += -Wno-missing-field-initializers
endif

ifeq (${CXX}, clang++)
	CXXFLAGS += -Wimplicit-fallthrough
endif

LDFLAGS := -s -Wl,--as-needed ${LDFLAGS}
LDLIBS := ${shell pkg-config --libs ${GTK} ${VTE}}

termite: termite.cc url_regex.hh util/clamp.hh util/maybe.hh util/memory.hh
	${CXX} ${CXXFLAGS} ${LDFLAGS} $< ${LDLIBS} -o $@

install: termite termite.desktop termite.terminfo
	mkdir -p ${DESTDIR}${TERMINFO}
	install -Dm755 termite ${DESTDIR}${BINDIR}/termite
	install -Dm644 config ${DESTDIR}/etc/xdg/termite/config
	install -Dm644 termite.desktop ${DESTDIR}${DATADIR}/applications/termite.desktop
	install -Dm644 man/termite.1 ${DESTDIR}${MANDIR}/man1/termite.1
	install -Dm644 man/termite.config.5 ${DESTDIR}${MANDIR}/man5/termite.config.5
	tic -x -o ${DESTDIR}${TERMINFO} termite.terminfo

uninstall:
	rm -f ${DESTDIR}${BINDIR}/termite

clean:
	rm termite

.PHONY: clean install uninstall
