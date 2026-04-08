# ShellBeats Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lncursesw -lcurl -lcjson -pthread

# macOS: Homebrew paths
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -I/opt/homebrew/include
    LDFLAGS += -L/opt/homebrew/lib
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
