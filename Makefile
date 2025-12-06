# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -pedantic -Wall -Wextra -MMD -MP
LDFLAGS =

# Directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin
TESTDIR = tests

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))
DEPENDS = $(OBJECTS:.o=.d)

# Target executable
TARGET = $(BINDIR)/vin

# Version
VERSION = 1.0.0

# Installation directories
PREFIX ?= /usr/local
BINDIR_INSTALL = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

# Build all
all: $(TARGET)

# Link executable
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories
$(BINDIR):
	@mkdir -p $(BINDIR)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# Clean build artifacts
clean:
	@rm -rf $(BUILDDIR) $(BINDIR)
	@echo "Cleaned build directories"

# Install system-wide
install: all
	@echo "Installing vin to $(BINDIR_INSTALL)..."
	@mkdir -p $(BINDIR_INSTALL)
	@cp $(TARGET) $(BINDIR_INSTALL)/vin
	@chmod 755 $(BINDIR_INSTALL)/vin
	@echo "Creating example config file..."
	@if [ ! -f ~/.vinrc ]; then \
		cp vinrc.example ~/.vinrc; \
		echo "Created ~/.vinrc (customize as needed)"; \
	else \
		echo "~/.vinrc already exists (not overwriting)"; \
	fi
	@echo ""
	@echo "✓ Installation complete!"
	@echo "  Run 'vin filename' to start editing"
	@echo "  Edit ~/.vinrc to customize keybindings and settings"

# Install to user directory (no sudo needed)
install-user: all
	@echo "Installing vin to ~/.local/bin..."
	@mkdir -p ~/.local/bin
	@cp $(TARGET) ~/.local/bin/vin
	@chmod 755 ~/.local/bin/vin
	@echo "Creating example config file..."
	@if [ ! -f ~/.vinrc ]; then \
		cp vinrc.example ~/.vinrc; \
		echo "Created ~/.vinrc (customize as needed)"; \
	else \
		echo "~/.vinrc already exists (not overwriting)"; \
	fi
	@echo ""
	@echo "✓ Installation complete!"
	@echo "  Add ~/.local/bin to your PATH if not already:"
	@echo "  echo 'export PATH=\"\$$HOME/.local/bin:\$$PATH\"' >> ~/.bashrc"
	@echo "  source ~/.bashrc"
	@echo ""
	@echo "  Then run 'vin filename' to start editing"

# Uninstall from system
uninstall:
	@echo "Removing vin from $(BINDIR_INSTALL)..."
	@rm -f $(BINDIR_INSTALL)/vin
	@echo "✓ Uninstalled (config file ~/.vinrc left intact)"

# Uninstall from user directory
uninstall-user:
	@echo "Removing vin from ~/.local/bin..."
	@rm -f ~/.local/bin/vin
	@echo "✓ Uninstalled (config file ~/.vinrc left intact)"

# Run the editor
run: $(TARGET)
	@$(TARGET) $(FILE)

# Display version
version:
	@echo "vin version $(VERSION)"

# Help
help:
	@echo "vin - Lightweight terminal text editor"
	@echo ""
	@echo "Build targets:"
	@echo "  make              - Build the editor"
	@echo "  make clean        - Remove build files"
	@echo "  make install      - Install system-wide (requires sudo)"
	@echo "  make install-user - Install to ~/.local/bin (no sudo)"
	@echo "  make uninstall    - Remove from system"
	@echo "  make run FILE=x   - Run editor on file x"
	@echo ""
	@echo "After installation:"
	@echo "  vin filename      - Open or create a file"
	@echo "  vin --help        - Show usage information"

# Include dependencies
-include $(DEPENDS)

.PHONY: all clean install install-user uninstall uninstall-user run version help
