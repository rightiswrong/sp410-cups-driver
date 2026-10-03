# sp410-cups-driver - open-source CUPS driver for iDPRT SP410-family printers
# SPDX-License-Identifier: Apache-2.0
#
#   make                 build the filter and test helpers
#   make check           run the test suite (no printer needed)
#   sudo make install    install filter, PPDs, udev rule, tools
#   make deb             build a .deb for the host architecture
#
# Override CUPS_CFLAGS / CUPS_LIBS if cups-config and pkg-config are missing.

VERSION    := $(shell cat VERSION)
PREFIX     ?= /usr
DESTDIR    ?=
CC         ?= cc
PYTHON     ?= python3

CUPS_CONFIG ?= $(shell command -v cups-config 2>/dev/null)
ifneq ($(CUPS_CONFIG),)
  CUPS_CFLAGS    ?= $(shell $(CUPS_CONFIG) --cflags)
  CUPS_LIBS      ?= $(shell $(CUPS_CONFIG) --libs)
  CUPS_SERVERBIN ?= $(shell $(CUPS_CONFIG) --serverbin)
else
  CUPS_CFLAGS    ?= $(shell pkg-config --cflags cups 2>/dev/null)
  CUPS_LIBS      ?= $(shell pkg-config --libs cups 2>/dev/null || echo -lcups)
  CUPS_SERVERBIN ?= $(PREFIX)/lib/cups
endif

FILTERDIR  ?= $(CUPS_SERVERBIN)/filter
PPDDIR     ?= $(PREFIX)/share/ppd/sp410-cups-driver
UDEVDIR    ?= $(PREFIX)/lib/udev/rules.d
BINDIR     ?= $(PREFIX)/bin
DOCDIR     ?= $(PREFIX)/share/doc/sp410-cups-driver

WARN       := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes \
              -Wmissing-prototypes -Wno-unused-parameter
CFLAGS     ?= -O2 -g
ALL_CFLAGS := -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
              -DSP410_VERSION=\"$(VERSION)\" $(WARN) $(CUPS_CFLAGS) $(CFLAGS)
ALL_LIBS   := $(CUPS_LIBS) -lm $(LDLIBS)

BUILD      := build
FILTER     := $(BUILD)/sp410-rastertotspl
MKRASTER   := $(BUILD)/mkraster
PPDCHECK   := $(BUILD)/ppdcheck

SRCS       := src/rastertotspl.c src/settings.c src/dither.c src/tspl.c
OBJS       := $(SRCS:src/%.c=$(BUILD)/%.o)
PPDS       := $(wildcard ppd/*.ppd)

.PHONY: all check ppd ppd-check install uninstall deb dist clean

all: $(FILTER) $(MKRASTER) $(PPDCHECK)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

$(FILTER): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ALL_LIBS)

$(MKRASTER): tests/mkraster.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -o $@ $< $(LDFLAGS) $(ALL_LIBS)

$(PPDCHECK): tests/ppdcheck.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -o $@ $< $(LDFLAGS) $(ALL_LIBS)

ppd:
	$(PYTHON) ppd/gen_ppd.py

ppd-check:
	$(PYTHON) ppd/gen_ppd.py --check

check: all ppd-check
	$(PPDCHECK) $(PPDS)
	$(PYTHON) tests/run_tests.py --filter $(FILTER) --mkraster $(MKRASTER)

install: $(FILTER)
	install -d $(DESTDIR)$(FILTERDIR) $(DESTDIR)$(PPDDIR) $(DESTDIR)$(UDEVDIR) \
	           $(DESTDIR)$(BINDIR) $(DESTDIR)$(DOCDIR)
	install -m 0755 $(FILTER) $(DESTDIR)$(FILTERDIR)/
	install -m 0644 $(PPDS) $(DESTDIR)$(PPDDIR)/
	install -m 0644 udev/60-idprt-sp410.rules $(DESTDIR)$(UDEVDIR)/
	install -m 0755 tools/sp410ctl.py $(DESTDIR)$(BINDIR)/sp410ctl
	install -m 0755 tools/tspl_decode.py $(DESTDIR)$(BINDIR)/tspl-decode
	install -m 0644 README.md docs/PROTOCOL.md docs/HARDWARE.md $(DESTDIR)$(DOCDIR)/

uninstall:
	rm -f $(DESTDIR)$(FILTERDIR)/sp410-rastertotspl
	rm -rf $(DESTDIR)$(PPDDIR) $(DESTDIR)$(DOCDIR)
	rm -f $(DESTDIR)$(UDEVDIR)/60-idprt-sp410.rules
	rm -f $(DESTDIR)$(BINDIR)/sp410ctl $(DESTDIR)$(BINDIR)/tspl-decode

deb: $(FILTER)
	packaging/build-deb.sh $(VERSION)

dist:
	git archive --format=tar.gz --prefix=sp410-cups-driver-$(VERSION)/ \
	    -o $(BUILD)/sp410-cups-driver-$(VERSION).tar.gz HEAD

clean:
	rm -rf $(BUILD)
