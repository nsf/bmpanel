SRCDIR := src

# included from .mk/config.mk
CFLAGS :=
LIBS :=

# included from $(SRCDIR)/Makefile
TARGETS :=

# define in $(SRCDIR)/Makefile
DEPS :=

CC := gcc
LD := gcc

all: setup srcs

clean:
	rm -rf bmpanel
	rm -R build

setup:
	@mkdir -p build $(patsubst %,build/%,$(SRCDIR))

.mk/config.mk:
	./configure

.PHONY: all setup srcs

-include .mk/config.mk
-include $(patsubst %,%/Makefile,$(SRCDIR))
-include $(DEPS)

install:
	@echo installing bmpanel to $(DESTDIR)$(PREFIX)/bin
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp -f bmpanel $(DESTDIR)$(PREFIX)/bin
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/bmpanel
	@echo installing themes to $(DESTDIR)$(PREFIX)/share/bmpanel
	@mkdir -p $(DESTDIR)$(PREFIX)/share/bmpanel
	@cp -R themes $(DESTDIR)$(PREFIX)/share/bmpanel

srcs: $(TARGETS)
