# ShellBeats Makefile
CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2 -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS: Homebrew paths + plain ncurses (already wide-char on macOS).
    # Detect Homebrew prefix (Apple Silicon /opt/homebrew, Intel /usr/local).
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
    CFLAGS  += -I$(BREW_PREFIX)/include -I$(BREW_PREFIX)/opt/ncurses/include
    LDFLAGS  = -L$(BREW_PREFIX)/lib -L$(BREW_PREFIX)/opt/ncurses/lib -lncurses -lcurl -lcjson -pthread
else
    # Linux: needs the wide-char variant explicitly
    LDFLAGS  = -lncursesw -lcurl -lcjson -pthread
endif

TARGET = shellbeats
SRC = shellbeats.c youtube_playlist.c surikata_sync.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: $(SRC)
	$(CC) $(CFLAGS) -g -DDEBUG -o $(TARGET) $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
