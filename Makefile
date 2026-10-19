CXX ?= clang++
MACOS_MIN := $(shell sw_vers -productVersion)
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -mmacosx-version-min=$(MACOS_MIN)
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)
SRC := $(wildcard src/*.cpp)
HDR := $(wildcard src/*.h)

.PHONY: all run debug clean

all: pixelworld

pixelworld: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(RAYLIB_CFLAGS) $(SRC) -o $@ $(RAYLIB_LIBS)

# Builds with AddressSanitizer + UndefinedBehaviorSanitizer for chasing crashes.
pixelworld-debug: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -g -O0 -fsanitize=address,undefined $(RAYLIB_CFLAGS) $(SRC) -o $@ $(RAYLIB_LIBS)

run: pixelworld
	./pixelworld

debug: pixelworld-debug

clean:
	rm -f pixelworld pixelworld-debug selftest.png smoke.png
