.POSIX:
.SUFFIXES:

include config.mk

DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) \
	-Iinclude -Iconfig -Isrc -Iprotocols/generated -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L -DVERSION=\"$(VERSION)\" \
	$(XWAYLAND) -g -Wall -Wextra -Wno-unused-parameter -O1 -std=c11 $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

SMSG_CFLAGS = `$(PKG_CONFIG) --cflags wayland-client` -Wall -Wextra -Wno-unused-parameter -Itools/monomsg/include
SMSG_LDLIBS = `$(PKG_CONFIG) --libs wayland-client`

MONO_BUILD = build
SMSG_BUILD = tools/monomsg/build

all: $(MONO_BUILD)/mono $(SMSG_BUILD)/monomsg

$(MONO_BUILD) $(SMSG_BUILD):
	mkdir -p $@

$(MONO_BUILD)/mono: $(MONO_BUILD)/main.o $(MONO_BUILD)/util.o \
	$(MONO_BUILD)/parser.o $(MONO_BUILD)/workspace.o \
	$(MONO_BUILD)/dwl-ipc-unstable-v2-protocol.o \
	$(MONO_BUILD)/ext-workspace-v1-protocol.o | $(MONO_BUILD)
	$(CC) $^ $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

$(MONO_BUILD)/main.o: src/main.c include/mono/client.h config/config.h include/mono/ipc.h \
	protocols/generated/dwl-ipc-unstable-v2-protocol.h \
	include/mono/ext.h include/mono/workspace.h protocols/generated/ext-workspace-v1-protocol.h \
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

protocols/generated/cursor-shape-v1-protocol.h:
	$(SCANNER) enum-header $(PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
protocols/generated/pointer-constraints-unstable-v1-protocol.h:
	$(SCANNER) enum-header $(PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
protocols/generated/wlr-layer-shell-unstable-v1-protocol.h:
	$(SCANNER) enum-header protocols/xml/wlr-layer-shell-unstable-v1.xml $@
protocols/generated/wlr-output-power-management-unstable-v1-protocol.h:
	$(SCANNER) server-header protocols/xml/wlr-output-power-management-unstable-v1.xml $@
protocols/generated/xdg-shell-protocol.h:
	$(SCANNER) server-header $(PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
protocols/generated/dwl-ipc-unstable-v2-protocol.h:
	$(SCANNER) server-header protocols/xml/dwl-ipc-unstable-v2.xml $@
protocols/generated/dwl-ipc-unstable-v2-protocol.c:
	$(SCANNER) private-code protocols/xml/dwl-ipc-unstable-v2.xml $@
protocols/generated/ext-workspace-v1-protocol.h:
	$(SCANNER) server-header protocols/xml/ext-workspace-v1.xml $@
protocols/generated/ext-workspace-v1-protocol.c:
	$(SCANNER) private-code protocols/xml/ext-workspace-v1.xml $@

tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h:
	$(SCANNER) client-header tools/monomsg/protocols/dwl-ipc-unstable-v2.xml $@
tools/monomsg/include/dwl-ipc-unstable-v2-protocol.c:
	$(SCANNER) private-code tools/monomsg/protocols/dwl-ipc-unstable-v2.xml $@
$(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o: tools/monomsg/include/dwl-ipc-unstable-v2-protocol.c | $(SMSG_BUILD)
	$(CC) $(SMSG_CFLAGS) -o $@ -c $<
$(SMSG_BUILD)/main.o: tools/monomsg/src/main.c tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h tools/monomsg/include/arg.h | $(SMSG_BUILD)
	$(CC) $(SMSG_CFLAGS) -o $@ -c $<
$(SMSG_BUILD)/monomsg: $(SMSG_BUILD)/main.o $(SMSG_BUILD)/dwl-ipc-unstable-v2-protocol.o | $(SMSG_BUILD)
	$(CC) $^ $(SMSG_CFLAGS) $(SMSG_LDLIBS) -o $@

clean:
	rm -rf $(MONO_BUILD) $(SMSG_BUILD)
	rm -f mono tools/monomsg/monomsg src/*.o protocols/generated/*.o \
		tools/monomsg/src/*.o tools/monomsg/include/*.o \
		tools/monomsg/include/*-protocol.h tools/monomsg/include/*-protocol.c \
		protocols/generated/*-protocol.h protocols/generated/*-protocol.c

install: $(MONO_BUILD)/mono $(SMSG_BUILD)/monomsg
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(MONO_BUILD)/mono $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mono
	cp -f $(SMSG_BUILD)/monomsg $(DESTDIR)$(PREFIX)/bin/monomsg
	chmod 755 $(DESTDIR)$(PREFIX)/bin/monomsg
	mkdir -p $(DESTDIR)/etc/mono
	cp -r examples/* $(DESTDIR)/etc/mono

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mono
	rm -f $(DESTDIR)$(PREFIX)/bin/monomsg
	rm -rf $(DESTDIR)/etc/mono
