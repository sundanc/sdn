CC = gcc
CFLAGS = -Wall -Wextra -O2
TERMINAL_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 vte-2.91)
TERMINAL_LIBS = $(shell pkg-config --libs gtk+-3.0 vte-2.91)

.PHONY: all clean install uninstall release

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
	rm -f $(HOME)/.local/bin/sdn $(HOME)/.local/bin/sdn_terminal
	cp sdn $(HOME)/.local/bin/
	cp sdn_terminal $(HOME)/.local/bin/
	mkdir -p $(HOME)/.local/share/applications/
	cp sdn.desktop $(HOME)/.local/share/applications/ || true
	mkdir -p $(HOME)/.local/share/icons/hicolor/48x48/apps/
	cp sdn_terminal.png $(HOME)/.local/share/icons/hicolor/48x48/apps/sdn_terminal.png || true
	@echo "Installed to $(HOME)/.local/bin"
	@echo "Updating desktop database..."
	@update-desktop-database -q $(HOME)/.local/share/applications || echo "Failed to update desktop database. Please run 'update-desktop-database ~/.local/share/applications' manually if the app doesn't appear."

# Uninstall applications
uninstall:
	rm -f $(HOME)/.local/bin/sdn
	rm -f $(HOME)/.local/bin/sdn_terminal
	rm -f $(HOME)/.local/share/applications/sdn.desktop
	rm -f $(HOME)/.local/share/icons/hicolor/48x48/apps/sdn_terminal.png
	echo "Uninstalled from $(HOME)/.local/bin"

# Clean build artifacts
clean:
	rm -f sdn sdn_terminal

# Rule to run the terminal
run: sdn_terminal
	./sdn_terminal

# Create a release tarball
RELEASE_NAME = sdn-0.1
RELEASE_FILES = sdn sdn_terminal README.md LICENSE.md Makefile sdn.desktop sdn_terminal.png
RELEASE_ARCHIVE = $(RELEASE_NAME).tar.gz

release: all
	@echo "Creating release archive: $(RELEASE_ARCHIVE)"
	rm -rf $(RELEASE_NAME) $(RELEASE_ARCHIVE)
	mkdir -p $(RELEASE_NAME)
	cp $(RELEASE_FILES) $(RELEASE_NAME)/
	tar -czf $(RELEASE_ARCHIVE) $(RELEASE_NAME)
	rm -rf $(RELEASE_NAME)
	@echo "Release archive created: $(RELEASE_ARCHIVE)"
