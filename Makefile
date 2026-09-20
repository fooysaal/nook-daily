CC      ?= gcc
VERSION  = 0.2.0
PKGS     = gtk+-3.0
PREFIX  ?= $(HOME)/.local
ICONDIR ?= $(CURDIR)/data/icons

CFLAGS  += -std=c11 -Wall -Wextra -O2 -DNOOK_ICON_DIR='"$(ICONDIR)"' -DNOOK_VERSION='"$(VERSION)"' \
           $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

SRC = src/main.c src/store.c src/tray.c
OBJ = $(SRC:.c=.o)

nook: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

src/%.o: src/%.c src/store.h src/tray.h
	$(CC) $(CFLAGS) -c $< -o $@

run: nook
	./nook

install:
	$(MAKE) clean
	$(MAKE) ICONDIR=$(PREFIX)/share/icons
	install -Dm755 nook $(DESTDIR)$(PREFIX)/bin/nook
	for png in data/icons/hicolor/*/apps/nook.png; do \
	  sz=$$(basename $$(dirname $$(dirname $$png))); \
	  install -Dm644 $$png $(DESTDIR)$(PREFIX)/share/icons/hicolor/$$sz/apps/nook.png; \
	done
	-gtk-update-icon-cache -qtf $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true
	install -d $(DESTDIR)$(PREFIX)/share/applications
	sed 's|^Exec=nook$$|Exec=$(PREFIX)/bin/nook|' data/nook.desktop \
	  > $(DESTDIR)$(PREFIX)/share/applications/nook.desktop
	chmod 644 $(DESTDIR)$(PREFIX)/share/applications/nook.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/nook
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/*/apps/nook.png
	rm -f $(DESTDIR)$(PREFIX)/share/applications/nook.desktop
	rm -f $(HOME)/.config/autostart/nook.desktop

check: src/store.c src/test_store.c
	$(CC) -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags glib-2.0) \
	  src/store.c src/test_store.c -o test_store $(shell pkg-config --libs glib-2.0)
	./test_store

clean:
	rm -f $(OBJ) nook test_store

.PHONY: run check install uninstall clean
