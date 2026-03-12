# ShellBeats Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lncursesw -pthread

PREFIX ?= /usr/local
LOCALEDIR ?= locale

TARGET = shellbeats
SRC = shellbeats.c youtube_playlist.c

LANGUAGES = hu
PO_DIR = po
LOCALE_DIR = locale

.PHONY: all clean install uninstall locale

all: $(TARGET) locale

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -DLOCALEDIR=\"$(LOCALEDIR)\" -o $@ $^ $(LDFLAGS)

debug: $(SRC)
	$(CC) $(CFLAGS) -g -DDEBUG -DLOCALEDIR=\"$(LOCALEDIR)\" -o $(TARGET) $^ $(LDFLAGS)

locale: $(foreach lang,$(LANGUAGES),$(LOCALE_DIR)/$(lang)/LC_MESSAGES/shellbeats.mo)

$(LOCALE_DIR)/%/LC_MESSAGES/shellbeats.mo: $(PO_DIR)/%.po
	@mkdir -p $(dir $@)
	msgfmt -o $@ $<

pot:
	xgettext --keyword=_ --language=C --add-comments \
		--from-code=UTF-8 \
		-o $(PO_DIR)/shellbeats.pot $(SRC)

clean:
	rm -f $(TARGET)
	rm -rf $(LOCALE_DIR)

install: $(TARGET) locale
	install -m 755 $(TARGET) $(PREFIX)/bin/
	@for lang in $(LANGUAGES); do \
		install -d $(PREFIX)/share/locale/$$lang/LC_MESSAGES/; \
		install -m 644 $(LOCALE_DIR)/$$lang/LC_MESSAGES/shellbeats.mo \
			$(PREFIX)/share/locale/$$lang/LC_MESSAGES/; \
	done

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	@for lang in $(LANGUAGES); do \
		rm -f $(PREFIX)/share/locale/$$lang/LC_MESSAGES/shellbeats.mo; \
	done
