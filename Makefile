CXX ?= c++
PACKAGES := hyprland pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon
CPPFLAGS += -Iinclude $(shell pkg-config --cflags $(PACKAGES))
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++26 -fPIC -Wall -Wextra -Wno-unused-parameter
SOURCES := src/Plugin.cpp src/Engine.cpp src/Hooks.cpp src/Layout.cpp

.PHONY: all test
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
