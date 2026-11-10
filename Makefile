# ==============================================================================
# C0 Compiler Build & Profiling Orchestration Makefile
# ==============================================================================

SHELL := /usr/bin/env bash
.SHELLFLAGS := -eu -o pipefail -c

# Directories
ROOT_DIR          := $(shell pwd)
BUILD_DIR         ?= build
DEBUG_DIR         := $(BUILD_DIR)/debug
RELEASE_DIR       := $(BUILD_DIR)/release
RELWITHDEB_DIR    := $(BUILD_DIR)/relwithdebinfo
PERF_DIR          ?= perf_data

# Executables
DEBUG_BIN         := $(DEBUG_DIR)/c0c
RELEASE_BIN       := $(RELEASE_DIR)/c0c
RELWITHDEB_BIN    := $(RELWITHDEB_DIR)/c0c

# Profiling configuration (Hotspot integration)
PERF_RECORD       ?= taskset -c 0 perf record -F 499 -e cycles:u --call-graph dwarf,2048
PERF_ITERATIONS   ?= 10000

# Test files
VALID_TESTS       := $(wildcard tests/valid/*.c0)
INVALID_TESTS     := $(wildcard tests/invalid/*.c0)
ALL_TESTS         := $(VALID_TESTS) $(INVALID_TESTS)

.PHONY: all debug release relwithdebinfo clean help

all: release

# ------------------------------------------------------------------------------
# Target: debug
# Build with ASan, LSan, UBSan, and run full test suite with memory safety checks.
# ------------------------------------------------------------------------------
debug:
	@echo "=== [Debug] Configuring CMake with AddressSanitizer & UBSan ==="
	@cmake -B $(DEBUG_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Debug \
		-DENABLE_SANITIZERS=ON \
		-DCMAKE_CXX_FLAGS="-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
	@ln -sf build/debug/compile_commands.json compile_commands.json
	@echo "=== [Debug] Compiling c0c ==="
	@cmake --build $(DEBUG_DIR)
	@echo "=== [Debug] Running Compiler Test Suite under Sanitizers ==="
	@BUILD_DIR="$(DEBUG_DIR)" ./tests/test_compiler.sh
	@echo ">>> [Debug] All tests passed cleanly under ASan/UBSan!"

# ------------------------------------------------------------------------------
# Target: release
# Build prioritized for throughput (-O3 -march=native -DNDEBUG), run test suite.
# ------------------------------------------------------------------------------
release:
	@echo "=== [Release] Configuring CMake with Maximum Optimization ==="
	@cmake -B $(RELEASE_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS="-O3 -march=native -DNDEBUG"
	@echo "=== [Release] Compiling c0c ==="
	@cmake --build $(RELEASE_DIR)
	@echo "=== [Release] Running Compiler Test Suite ==="
	@BUILD_DIR="$(RELEASE_DIR)" ./tests/test_compiler.sh
	@echo ">>> [Release] Compiler built and verified at $(RELEASE_BIN)"

# ------------------------------------------------------------------------------
# Target: relwithdebinfo
# Build with profiling symbols (-O3 -g -fno-omit-frame-pointer), run perf record
# per test to output <test_name>.data for KDAB Hotspot bottleneck inspection.
# ------------------------------------------------------------------------------
relwithdebinfo:
	@echo "=== [RelWithDebInfo] Configuring CMake with Profiling Symbols ==="
	@cmake -B $(RELWITHDEB_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_CXX_FLAGS="-O3 -g -fno-omit-frame-pointer -DNDEBUG"
	@echo "=== [RelWithDebInfo] Compiling c0c ==="
	@cmake --build $(RELWITHDEB_DIR)
	@echo "=== [RelWithDebInfo] Verifying Test Suite Correctness ==="
	@BUILD_DIR="$(RELWITHDEB_DIR)" ./tests/test_compiler.sh
	@echo "=== [RelWithDebInfo] Recording perf.data per test for Hotspot ==="
	@mkdir -p $(PERF_DIR)
	@for test_file in $(ALL_TESTS); do \
		test_name=$$(basename "$$test_file" .c0); \
		output_data="$(PERF_DIR)/$${test_name}.data"; \
		if [ -f "$$output_data" ]; then \
			mv -f "$$output_data" "$$output_data.old"; \
		fi; \
		echo "  -> Profiling $$test_name ($$test_file) [$(PERF_ITERATIONS) iterations] -> $$output_data"; \
		$(PERF_RECORD) -o "$$output_data" -- \
			$(RELWITHDEB_BIN) --benchmark $(PERF_ITERATIONS) "$$test_file" > /dev/null 2>&1 || true; \
	done
	@echo ""
	@echo ">>> [RelWithDebInfo] Profiling complete! Generated data files in $(PERF_DIR)/:"
	@ls -lh $(PERF_DIR)/*.data | awk '{print "    " $$9 " (" $$5 ")"}'
	@echo ""
	@echo ">>> To inspect bottlenecks in Hotspot:"
	@echo "    hotspot $(PERF_DIR)/<test_name>.data"

# ------------------------------------------------------------------------------
# Housekeeping
# ------------------------------------------------------------------------------
clean:
	@echo "Cleaning build directories and perf data..."
	@rm -rf $(BUILD_DIR) $(PERF_DIR)
	@echo "Done."

help:
	@echo "C0 Compiler Build & Profiling Commands:"
	@echo "  make debug          Build with ASan/UBSan and run test suite"
	@echo "  make release        Build with -O3 -march=native and run test suite"
	@echo "  make relwithdebinfo Build with debug symbols and record perf.data for each test"
	@echo "  make clean          Remove build/ and perf_data/ directories"
	@echo ""
	@echo "Configurable Variables:"
	@echo "  PERF_DIR            Directory for perf output (default: perf_data)"
	@echo "  PERF_ITERATIONS     Loop count per test during perf recording (default: 10)"
