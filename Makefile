CC = cc
CPPFLAGS = -Iinclude -Isrc/sgf
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS =
COMMON = src/common/protocol.c src/common/display.c
HEADERS = $(wildcard include/*.h src/sgf/*.h)
.PHONY: all clean
all: bin/shell bin/ecran
bin build:
	mkdir -p $@
build/libsgf.a: build/storage.o build/commands.o
	$(AR) rcs $@ $^
build/storage.o: src/sgf/storage.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/commands.o: src/sgf/commands.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
bin/shell: src/shell/main.c src/noyau/kernel.c $(COMMON) build/libsgf.a $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) src/shell/main.c src/noyau/kernel.c $(COMMON) build/libsgf.a -o $@
bin/ecran: src/ecran/main.c src/common/display.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) src/ecran/main.c src/common/display.c -o $@
clean:
	rm -rf build bin
