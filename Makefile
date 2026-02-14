# ShellBeats Makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lncurses -pthread

TARGET = bin/shellbeats
SRC = $(wildcard src/*.c)

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -g -DDEBUG -o $(TARGET) $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/shellbeats

uninstall:
	rm -f /usr/local/bin/shellbeats
