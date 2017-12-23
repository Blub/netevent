-include ./config.mak
-include ./local-env.mak

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATAROOTDIR = $(PREFIX)/share
MANDIR = $(DATAROOTDIR)/man
MAN1DIR = $(MANDIR)/man1

CPPFLAGS ?= -g
CPPFLAGS += -Wall -Werror -Wno-unknown-pragmas
CXX ?= clang++
# Code should compile with c++11 as well, but c++14 may have stricter
# attributes on some methods.
CXXFLAGS += -std=c++14

ifeq ($(CXX), clang++)
CPPFLAGS += -Weverything \
            -Wno-c++98-compat \
            -Wno-c++98-compat-pedantic \
            -Wno-padded
endif

LDFLAGS ?= -g

SANITIZE_FLAGS ?=

CPPFLAGS += $(SANITIZE_FLAGS)
LDFLAGS += $(SANITIZE_FLAGS)

BINARY := netevent
OBJECTS := src/main.o \
           src/daemon.o \
           src/writer.o \
           src/reader.o \
           src/socket.o

MAN1PAGES-y := doc/netevent.1

MAN1PAGES := $(MAN1PAGES-$(ENABLE_DOC))

all: $(BINARY) $(MAN1PAGES)

config.h:
	./configure

# the src/*.d files contain 'src/../config.h' for this:
src/../config.h: config.h

$(BINARY): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $< -MMD -MT $@ -MF $(@:.o=.d)

.SUFFIXES: .1 .rst
.rst.1:
	$(RST2MAN) $< $@

.PHONY: install
install: # Make sure we use config.mak, nest once:
	$(MAKE) install-do
.PHONY: install-do
install-do: $(BINARY) $(MAN1PAGES)
	install -dm755 $(DESTDIR)
	install -dm755 $(DESTDIR)$(BINDIR)
	install -Tm755 $(BINARY) $(DESTDIR)$(BINDIR)/$(BINARY)
	install -dm755 $(DESTDIR)$(MAN1DIR)
	for i in $(MAN1PAGES); do \
	  install -Tm755 $$i $(DESTDIR)$(MAN1DIR) \
	done

distclean: clean
	rm -f config.h

clean:
	rm -f src/*.o src/*.d doc/*.1

$(OBJECTS): config.h

-include src/*.d
