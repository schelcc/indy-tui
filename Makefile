BUILD_DIR := ./build
BUILD_TYPE := Release
EXTRA_CMAKE_FLAGS :=
TEST_PREFIX := 
EXTRA_TEST_FLAGS :=

# Add to when deferring to base_build
CXX_FLAGS := -Wall -Wextra

.PHONY: build rebuild debug hard_clean soft_clean clean base_build display_only tests fix_cc dbg_tests

default: build

build: BUILD_TYPE := Release
build: CXX_FLAGS += -DNDEBUG -O3
build: base_build


debug: BUILD_TYPE := Debug
debug: EXTRA_CXX_FLAGS += -Wpedantic -Wconversion -Weffc++
debug: EXTRA_CMAKE_FLAGS += -DBUILD_MAIN=ON -DBUILD_REPLAY_SERVER=ON -DBUILD_TESTS=OFF -DBUILD_DISP_DEMO=OFF
debug: base_build


display_only: CXX_FLAGS += -Wl,--copy-dt-needed-entries
display_only: EXTRA_CMAKE_FLAGS += -DBUILD_MAIN=OFF -DBUILD_REPLAY_SERVER=OFF -DBUILD_TESTS=OFF
display_only: EXTRA_CMAKE_FLAGS += -DBUILD_DISP_DEMO=ON
display_only: base_build

dbg_tests: TEST_PREFIX := gdb -ex run --args
dbg_tests: EXTRA_TEST_FLAGS += --break
dbg_tests: tests

tests: BUILD_TYPE := Debug
tests: EXTRA_CXX_FLAGS += -Wpedantic -Wconversion -Weffc++
tests: EXTRA_CMAKE_FLAGS += -DBUILD_MAIN=OFF -DBUILD_REPLAY_SERVER=OFF -DBUILD_DISP_DEMO=OFF -DBUILD_TESTS=ON
tests: base_build
tests:
	$(TEST_PREFIX) ./build/tests/tests $(EXTRA_TEST_FLAGS)



base_build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_CXX_FLAGS="$(CXX_FLAGS)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(EXTRA_CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --parallel
base_build: fix_cc

fix_cc:
	cp build/compile_commands.json ./compile_commands.json

# Default clean to a soft clean
clean: soft_clean

# Remove everything in build except for _deps/ and any hidden files/directories
soft_clean: 
	find $(BUILD_DIR) -maxdepth 1 -mindepth 1 -not -name "_deps" -not -path '*/.*' -exec rm -rf {} \;
