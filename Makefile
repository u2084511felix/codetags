CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread

# Get the current user
USER := $(shell whoami)
HOME := $(shell eval echo ~$(USER))

SOURCES = main.cpp
TARGET = codetags
REPODIR = $(HOME)/.ctags/registered_repos.txt
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SYSTEMD_DIR = /etc/systemd/system

.PHONY: all install uninstall clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	@echo "Installing codetags to $(BINDIR)..."
	@sudo install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "# Creating systemd service file..."
	@echo "[Unit]" > /tmp/codetags-daemon.service
	@echo "Description=Codetags Background Daemon" >> /tmp/codetags-daemon.service
	@echo "After=multi-user.target" >> /tmp/codetags-daemon.service
	@echo "" >> /tmp/codetags-daemon.service
	@echo "[Service]" >> /tmp/codetags-daemon.service
	@echo "Type=simple" >> /tmp/codetags-daemon.service
	@echo "ExecStart=$(BINDIR)/$(TARGET) daemon" >> /tmp/codetags-daemon.service
	@echo "Restart=always" >> /tmp/codetags-daemon.service
	@echo "User=$(USER)" >> /tmp/codetags-daemon.service
	@echo "Group=$(USER)" >> /tmp/codetags-daemon.service
	@echo "Environment=HOME=$(shell eval echo ~$(USER))" >> /tmp/codetags-daemon.service
	@echo "" >> /tmp/codetags-daemon.service
	@echo "[Install]" >> /tmp/codetags-daemon.service
	@echo "WantedBy=multi-user.target" >> /tmp/codetags-daemon.service
	@sudo install -m 644 /tmp/codetags-daemon.service $(SYSTEMD_DIR)/codetags-daemon.service
	@sudo systemctl daemon-reload
	@sudo systemctl enable codetags-daemon.service
	@rm -f /tmp/codetags-daemon.service
	@echo "Installation complete. Starting daemon..."
	@sudo systemctl start codetags-daemon.service
	@echo "Daemon started and enabled for auto-start on boot."

uninstall:
	@sudo systemctl stop codetags-daemon.service 2>/dev/null || true
	@sudo systemctl disable codetags-daemon.service 2>/dev/null || true
	@rm -f $(BINDIR)/$(TARGET)
	@sudo rm -f $(SYSTEMD_DIR)/codetags-daemon.service
	@sudo systemctl daemon-reload
	@echo "Uninstallation complete."

clean:
	rm -f $(TARGET)
	rm -f $(REPODIR)

# Default target - builds and installs
.DEFAULT_GOAL := install
