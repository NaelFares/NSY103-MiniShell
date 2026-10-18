# Outils et options de compilation du projet en C.
CC = cc
CPPFLAGS = -Icommun -Inoyau -Isgf
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS =

# Fichiers communs et en-tetes utilises par les programmes.
COMMON = commun/protocol.c commun/display.c
HEADERS = $(wildcard commun/*.h noyau/*.h sgf/*.h)

.PHONY: all clean

# make construit les deux programmes a lancer dans les terminaux.
all: bin/shell bin/ecran

# Les dossiers sont crees seulement s'ils n'existent pas encore.
bin build:
	mkdir -p $@

# On rassemble le stockage et les commandes dans la bibliotheque SGF.
build/libsgf.a: build/storage.o build/commands.o
	$(AR) rcs $@ $^

build/storage.o: sgf/storage.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/commands.o: sgf/commands.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Le Shell contient aussi le code du Noyau, execute par son fils.
bin/shell: shell/shell.c noyau/kernel.c $(COMMON) build/libsgf.a $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) shell/shell.c noyau/kernel.c $(COMMON) build/libsgf.a -o $@

# Ecran est un programme separe qui recoit les messages du Shell.
bin/ecran: ecran/ecran.c commun/display.c $(HEADERS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) ecran/ecran.c commun/display.c -o $@

# On retire les fichiers compiles pour repartir d'une compilation propre.
clean:
	rm -rf build bin
