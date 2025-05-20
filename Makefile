CC = gcc
CFLAGS = -Wall -Wextra -O2
TERMINAL_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 vte-2.91)
TERMINAL_LIBS = $(shell pkg-config --libs gtk+-3.0 vte-2.91)

.PHONY: all clean install uninstall

all: sdn sdn_terminal

# Compile the sdn shell
sdn: sdn.c
	$(CC) $(CFLAGS) -o $@ $<

# Compile the terminal application
sdn_terminal: sdn_terminal.c
	$(CC) $(CFLAGS) $(TERMINAL_CFLAGS) -o $@ $< $(TERMINAL_LIBS)

# Install both applications
install: sdn sdn_terminal
	mkdir -p $(HOME)/.local/bin
	cp sdn $(HOME)/.local/bin/
	cp sdn_terminal $(HOME)/.local/bin/
	cp sdn.desktop $(HOME)/.local/share/applications/ || true
	echo "Installed to $(HOME)/.local/bin"

# Uninstall applications
uninstall:
	rm -f $(HOME)/.local/bin/sdn
	rm -f $(HOME)/.local/bin/sdn_terminal
	rm -f $(HOME)/.local/share/applications/sdn.desktop
	echo "Uninstalled from $(HOME)/.local/bin"

# Clean build artifacts
clean:
	rm -f sdn sdn_terminal

# Rule to run the terminal
run: sdn_terminal
	./sdn_terminal
