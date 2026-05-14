BUILD_DIR := ./build
BUILD_TYPE := Release

# Add to when deferring to base_build
CXX_FLAGS := -Wall -Wextra

.PHONY: build rebuild debug hard_clean soft_clean clean base_build 

default: build

build: BUILD_TYPE := Release
build: CXX_FLAGS += -DNDEBUG -O3
build: base_build

debug: BUILD_TYPE := Debug
debug: EXTRA_CXX_FLAGS += -Wpedantic -Wconversion -Weffc++
debug: base_build

base_build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_CXX_FLAGS="$(CXX_FLAGS)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) --parallel

# Default clean to a soft clean
clean: soft_clean

# Remove everything in build except for _deps/ and any hidden files/directories
soft_clean: 
	find $(BUILD_DIR) -maxdepth 1 -mindepth 1 -not -name "_deps" -not -path '*/.*' -exec rm -rf {} \;
