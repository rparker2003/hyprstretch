CXX ?= c++
PACKAGES := hyprland pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon
CPPFLAGS += -Iinclude $(shell pkg-config --cflags $(PACKAGES))
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++26 -fPIC -Wall -Wextra -Wno-unused-parameter
SOURCES := src/Plugin.cpp src/Engine.cpp src/Hooks.cpp src/Layout.cpp

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
PLUGINDIR ?= $(PREFIX)/lib/hyprstretch

.PHONY: all test check install install-tools uninstall reload clean
all: build/hyprstretch.so

build:
	mkdir -p $@

build/hyprstretch.so: $(SOURCES) $(wildcard include/stretch/*.hpp) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared $(SOURCES) -o $@

build/layout-test: tests/layout.cpp src/Layout.cpp include/stretch/Layout.hpp | build
	$(CXX) -Iinclude $(CXXFLAGS) tests/layout.cpp src/Layout.cpp -o $@

build/window-fixture: tests/window.cpp | build
	$(CXX) -std=c++26 -Wall -Wextra tests/window.cpp -lxcb -o $@

test: build/layout-test
	./build/layout-test

check: test build/hyprstretch.so build/window-fixture
	python3 tests/picker_smoke.py
	python3 tests/integration.py

install-tools:
	install -d "$(BINDIR)"
	install -m 755 tools/toggle.py "$(BINDIR)/hyprstretch-toggle"
	install -m 755 tools/picker.py "$(BINDIR)/hyprstretch-picker"

install: build/hyprstretch.so install-tools
	install -d "$(PLUGINDIR)"
	@versioned="hyprstretch-$$(date +%s).so"; \
	install -m 755 build/hyprstretch.so "$(PLUGINDIR)/$$versioned"; \
	ln -sfn "$$versioned" "$(PLUGINDIR)/current.so"

uninstall:
	-hyprctl plugin unload "$(PLUGINDIR)/current.so" >/dev/null 2>&1
	rm -f "$(BINDIR)/hyprstretch-toggle" "$(BINDIR)/hyprstretch-picker" "$(PLUGINDIR)/current.so"

reload: build/hyprstretch.so
	install -d "$(PLUGINDIR)"
	-hyprctl plugin unload "$(PLUGINDIR)/current.so" >/dev/null 2>&1
	@versioned="hyprstretch-$$(date +%s).so"; \
	install -m 755 build/hyprstretch.so "$(PLUGINDIR)/$$versioned"; \
	ln -sfn "$$versioned" "$(PLUGINDIR)/current.so"
	hyprctl plugin load "$(PLUGINDIR)/current.so"

clean:
	rm -rf build
