.POSIX:
.SUFFIXES:

include config.mk

# POSIX mode supplies a built-in -O1; keep only explicitly supplied CFLAGS.
ifeq ($(origin CFLAGS),default)
CFLAGS =
endif

OPTFLAGS = -O2
DEBUGFLAGS = -g
BUILD_FLAGS = $(OPTFLAGS) $(DEBUGFLAGS) -Wall -Wextra -Wno-unused-parameter $(CFLAGS)

DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) \
	-Iinclude -Iconfig -Isrc -Iprotocols/generated -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L -DVERSION=\"$(VERSION)\" \
	$(XWAYLAND) -std=c11 $(BUILD_FLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
PROTOCOLS = $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)

SMSG_CFLAGS = `$(PKG_CONFIG) --cflags wayland-client` $(BUILD_FLAGS) -Itools/monomsg/include
SMSG_LDLIBS = `$(PKG_CONFIG) --libs wayland-client`

MONO_BUILD = build
SMSG_BUILD = tools/monomsg/build

all: $(MONO_BUILD)/mono $(SMSG_BUILD)/monomsg

OBJCOPY = objcopy
STRIP = strip

.PHONY: all check check-headless performance release install-release clean install uninstall

release: all
	mkdir -p $(MONO_BUILD)/release $(SMSG_BUILD)/release
	$(OBJCOPY) --only-keep-debug $(MONO_BUILD)/mono $(MONO_BUILD)/release/mono.debug
	$(STRIP) --strip-unneeded -o $(MONO_BUILD)/release/mono $(MONO_BUILD)/mono
	$(OBJCOPY) --add-gnu-debuglink=$(MONO_BUILD)/release/mono.debug $(MONO_BUILD)/release/mono
	$(OBJCOPY) --only-keep-debug $(SMSG_BUILD)/monomsg $(SMSG_BUILD)/release/monomsg.debug
	$(STRIP) --strip-unneeded -o $(SMSG_BUILD)/release/monomsg $(SMSG_BUILD)/monomsg
	$(OBJCOPY) --add-gnu-debuglink=$(SMSG_BUILD)/release/monomsg.debug $(SMSG_BUILD)/release/monomsg

install-release: release
	$(MAKE) INSTALL_MONO=$(MONO_BUILD)/release/mono INSTALL_MONOMSG=$(SMSG_BUILD)/release/monomsg install

performance: $(MONO_BUILD)/test-performance
	$(MONO_BUILD)/test-performance performance

$(MONO_BUILD)/test-performance: tests/animation.c tests/bugs.h tests/performance.h tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h $(MONO_BUILD)/mono
	$(CC) $(DWLCFLAGS) -DMONO_PERFORMANCE -o $@ tests/animation.c $(MONO_BUILD)/util.o \
		$(MONO_BUILD)/parser.o $(MONO_BUILD)/workspace.o \
		$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o \
		$(MONO_BUILD)/ext-workspace-v1-protocol.o $(LDFLAGS) $(LDLIBS) $(SMSG_LDLIBS) -Wl,--wrap=calloc,--wrap=free

check-headless: all $(MONO_BUILD)/test-headless-client
	python3 tests/headless.py $(MONO_BUILD)/mono $(SMSG_BUILD)/monomsg $(MONO_BUILD)/test-headless-client

$(MONO_BUILD)/test-xdg-shell.h: $(PROTOCOLS)/stable/xdg-shell/xdg-shell.xml | $(MONO_BUILD)
	$(SCANNER) client-header $< $@

$(MONO_BUILD)/test-xdg-shell.c: $(PROTOCOLS)/stable/xdg-shell/xdg-shell.xml | $(MONO_BUILD)
	$(SCANNER) private-code $< $@

$(MONO_BUILD)/test-headless-client: tests/headless-client.c $(MONO_BUILD)/test-xdg-shell.h $(MONO_BUILD)/test-xdg-shell.c
	$(CC) $(SMSG_CFLAGS) -I$(MONO_BUILD) -o $@ tests/headless-client.c $(MONO_BUILD)/test-xdg-shell.c $(LDFLAGS) $(SMSG_LDLIBS)

check: $(MONO_BUILD)/test-animation $(MONO_BUILD)/test-parser $(MONO_BUILD)/test-monomsg
	$(MONO_BUILD)/test-animation
	$(MONO_BUILD)/test-parser
	$(MONO_BUILD)/test-monomsg

$(MONO_BUILD)/test-animation: tests/animation.c tests/bugs.h tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h $(MONO_BUILD)/mono
	$(CC) $(DWLCFLAGS) -o $@ tests/animation.c $(MONO_BUILD)/util.o \
		$(MONO_BUILD)/parser.o $(MONO_BUILD)/workspace.o \
		$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o \
		$(MONO_BUILD)/ext-workspace-v1-protocol.o $(LDFLAGS) $(LDLIBS) $(SMSG_LDLIBS)

$(MONO_BUILD)/test-parser: tests/parser.c src/parser.c include/mono/parser.h | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ tests/parser.c $(LDFLAGS) $(LDLIBS)

$(MONO_BUILD)/test-monomsg: tests/monomsg.c tools/monomsg/src/main.c tools/monomsg/include/arg.h tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h $(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o | $(MONO_BUILD)
	$(CC) $(SMSG_CFLAGS) -o $@ tests/monomsg.c $(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o $(LDFLAGS) $(SMSG_LDLIBS)

$(MONO_BUILD) $(SMSG_BUILD) protocols/generated:
	mkdir -p $@

$(MONO_BUILD)/mono: $(MONO_BUILD)/main.o $(MONO_BUILD)/util.o \
	$(MONO_BUILD)/parser.o $(MONO_BUILD)/workspace.o \
	$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o \
	$(MONO_BUILD)/ext-workspace-v1-protocol.o | $(MONO_BUILD)
	$(CC) $^ $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

$(MONO_BUILD)/main.o: src/main.c include/mono/client.h config/config.h include/mono/ipc.h \
	protocols/generated/dwl-ipc-unstable-v2-protocol.h \
	include/mono/ext.h include/mono/animation.h include/mono/workspace.h protocols/generated/ext-workspace-v1-protocol.h \
	protocols/generated/cursor-shape-v1-protocol.h protocols/generated/pointer-constraints-unstable-v1-protocol.h \
	protocols/generated/wlr-layer-shell-unstable-v1-protocol.h \
	protocols/generated/wlr-output-power-management-unstable-v1-protocol.h protocols/generated/xdg-shell-protocol.h \
	include/mono/parser.h | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

$(MONO_BUILD)/util.o: src/util.c include/mono/util.h | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

$(MONO_BUILD)/workspace.o: src/workspace.c include/mono/workspace.h protocols/generated/ext-workspace-v1-protocol.h | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

$(MONO_BUILD)/parser.o: src/parser.c include/mono/parser.h | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o: protocols/generated/dwl-ipc-unstable-v2-protocol.c | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

$(MONO_BUILD)/ext-workspace-v1-protocol.o: protocols/generated/ext-workspace-v1-protocol.c | $(MONO_BUILD)
	$(CC) $(DWLCFLAGS) -o $@ -c $<

protocols/generated/cursor-shape-v1-protocol.h: $(PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml | protocols/generated
	$(SCANNER) enum-header $< $@
protocols/generated/pointer-constraints-unstable-v1-protocol.h: $(PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml | protocols/generated
	$(SCANNER) enum-header $< $@
protocols/generated/wlr-layer-shell-unstable-v1-protocol.h: protocols/xml/wlr-layer-shell-unstable-v1.xml | protocols/generated
	$(SCANNER) enum-header $< $@
protocols/generated/wlr-output-power-management-unstable-v1-protocol.h: protocols/xml/wlr-output-power-management-unstable-v1.xml | protocols/generated
	$(SCANNER) server-header $< $@
protocols/generated/xdg-shell-protocol.h: $(PROTOCOLS)/stable/xdg-shell/xdg-shell.xml | protocols/generated
	$(SCANNER) server-header $< $@
protocols/generated/dwl-ipc-unstable-v2-protocol.h: protocols/xml/dwl-ipc-unstable-v2.xml | protocols/generated
	$(SCANNER) server-header $< $@
protocols/generated/dwl-ipc-unstable-v2-protocol.c: protocols/xml/dwl-ipc-unstable-v2.xml | protocols/generated
	$(SCANNER) private-code $< $@
protocols/generated/ext-workspace-v1-protocol.h: protocols/xml/ext-workspace-v1.xml | protocols/generated
	$(SCANNER) server-header $< $@
protocols/generated/ext-workspace-v1-protocol.c: protocols/xml/ext-workspace-v1.xml | protocols/generated
	$(SCANNER) private-code $< $@

tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h: tools/monomsg/protocols/dwl-ipc-unstable-v2.xml
	$(SCANNER) client-header $< $@
tools/monomsg/include/dwl-ipc-unstable-v2-protocol.c: tools/monomsg/protocols/dwl-ipc-unstable-v2.xml
	$(SCANNER) private-code $< $@
$(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o: tools/monomsg/include/dwl-ipc-unstable-v2-protocol.c | $(SMSG_BUILD)
	$(CC) $(SMSG_CFLAGS) -o $@ -c $<
$(SMSG_BUILD)/main.o: tools/monomsg/src/main.c tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h tools/monomsg/include/arg.h | $(SMSG_BUILD)
	$(CC) $(SMSG_CFLAGS) -o $@ -c $<
$(SMSG_BUILD)/monomsg: $(SMSG_BUILD)/main.o $(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o | $(SMSG_BUILD)
	$(CC) $^ $(SMSG_CFLAGS) $(LDFLAGS) $(SMSG_LDLIBS) -o $@

clean:
	rm -rf $(MONO_BUILD) $(SMSG_BUILD)
	rm -f mono tools/monomsg/monomsg src/*.o protocols/generated/*.o \
		tools/monomsg/src/*.o tools/monomsg/include/*.o \
		tools/monomsg/include/*-protocol.h tools/monomsg/include/*-protocol.c \
		protocols/generated/*-protocol.h protocols/generated/*-protocol.c

INSTALL_MONO = $(MONO_BUILD)/mono
INSTALL_MONOMSG = $(SMSG_BUILD)/monomsg

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(INSTALL_MONO) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mono
	cp -f $(INSTALL_MONOMSG) $(DESTDIR)$(PREFIX)/bin/monomsg
	chmod 755 $(DESTDIR)$(PREFIX)/bin/monomsg
	mkdir -p $(DESTDIR)/etc/mono
	# keep an admin-edited config
	[ -e $(DESTDIR)/etc/mono/config.lua ] || \
		cp examples/config.lua $(DESTDIR)/etc/mono/config.lua

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mono
	rm -f $(DESTDIR)$(PREFIX)/bin/monomsg
	# only remove the config if it is still the unmodified example
	if cmp -s examples/config.lua $(DESTDIR)/etc/mono/config.lua; then \
		rm -f $(DESTDIR)/etc/mono/config.lua; fi
	rmdir $(DESTDIR)/etc/mono 2>/dev/null || true

# Rebuild objects/tests when build defaults or dependency settings change.
$(MONO_BUILD)/main.o \
	$(MONO_BUILD)/util.o \
	$(MONO_BUILD)/parser.o \
	$(MONO_BUILD)/workspace.o \
	$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o \
	$(MONO_BUILD)/ext-workspace-v1-protocol.o \
	$(SMSG_BUILD)/main.o \
	$(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o \
	$(MONO_BUILD)/test-animation \
	$(MONO_BUILD)/test-parser \
	$(MONO_BUILD)/test-performance \
	$(MONO_BUILD)/test-monomsg \
	$(MONO_BUILD)/test-headless-client: Makefile config.mk
