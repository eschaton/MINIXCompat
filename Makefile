.POSIX:

CC = clang

CFLAGS = -O2 -Wall
CPPFLAGS += -MMD -IMusashi
LIBS ::= -lm

CC_FOR_BUILD ?= $(CC)
CFLAGS_FOR_BUILD ?= $(CFLAGS)
CPPFLAGS_FOR_BUILD ?= $(CPPFLAGS)
LDFLAGS_FOR_BUILD ?= $(LDFLAGS)

PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= $(EXEC_PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
MANDIR ?= $(DATAROOTDIR)/man

.SUFFIXES:
.SUFFIXES: .c .o

MINIXCOMPAT_SRC != find MINIXCompat -name '*.c'
MINIXCOMPAT_OBJ ::= $(MINIXCOMPAT_SRC:.c=.o)
MINIXCOMPAT_BIN ::= MINIXCompat/MINIXCompat

MUSASHI_SRC ::= Musashi/m68kcpu.c Musashi/m68kdasm.c Musashi/softfloat/softfloat.c
MUSASHI_OBJ ::= $(MUSASHI_SRC:.c=.o)

all: $(MINIXCOMPAT_BIN)
.PHONY: all

$(MINIXCOMPAT_BIN): $(MINIXCOMPAT_OBJ) $(MUSASHI_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

MINIXCompat/MINIXCompat_EmulationOps.o: Musashi/m68kops.c
Musashi/m68kcpu.o: Musashi/m68kops.h

MUSASHI_GEN_SRC ::= Musashi/m68kops.h Musashi/m68kops.c
$(MUSASHI_GEN_SRC): Musashi/m68k_in.c Musashi/m68kmake
	Musashi/m68kmake Musashi $<

Musashi/m68kmake: Musashi/m68kmake.c
	$(CC_FOR_BUILD) $(CPPFLAGS_FOR_BUILD) $(CFLAGS_FOR_BUILD) $(LDFLAGS_FOR_BUILD) -o $@ $<

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

clean:
	rm -f $(MINIXCOMPAT_BIN) Musashi/m68kmake $(MUSASHI_GEN_SRC) $(MUSASHI_OBJ) $(MINIXCOMPAT_OBJ)
	rm -f $(MINIXCOMPAT_SRC:.c=.d) $(MUSASHI_SRC:.c=.d) Musashi/m68kmake.d
distclean: clean
.PHONY: clean distclean

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	install $(MINIXCOMPAT_BIN) $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp MINIXCompat/MINIXCompat.1 $(DESTDIR)$(MANDIR)/man1
.PHONY: install

-include $(MINIXCOMPAT_SRC:.c=.d)
-include $(MUSASHI_SRC:.c=.d)
