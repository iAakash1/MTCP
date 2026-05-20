# ============================================================================
# MTCP — Production-Style Multithreaded TCP Server
# Modern Makefile with:
#   - debug/release builds
#   - sanitizers
#   - benchmark helpers
#   - stress testing
#   - formatting
#   - dependency visualization
# ============================================================================

# ----------------------------------------------------------------------------
# Compiler Configuration
# ----------------------------------------------------------------------------

CXX := g++

BASE_FLAGS := -std=c++17 -Wall -Wextra -Wpedantic
DEBUG_FLAGS := -O1 -g -fsanitize=address -fsanitize=undefined
RELEASE_FLAGS := -O3 -DNDEBUG

CXXFLAGS := $(BASE_FLAGS) $(RELEASE_FLAGS)

LDFLAGS := -pthread

# ----------------------------------------------------------------------------
# Project Structure
# ----------------------------------------------------------------------------

SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj
TARGET  := server

# Recursively find all source files
SOURCES := $(shell find $(SRC_DIR) -name '*.cpp')

# Generate matching object paths
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))

# ----------------------------------------------------------------------------
# Default Target
# ----------------------------------------------------------------------------

.PHONY: all
all: $(TARGET)

# ----------------------------------------------------------------------------
# Linking
# ----------------------------------------------------------------------------

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

	@echo ""
	@echo "================================================================"
	@echo "  Build successful -> ./$(TARGET)"
	@echo "================================================================"
	@echo "  Build modes:"
	@echo "    make           -> Release build"
	@echo "    make debug     -> Sanitizer build"
	@echo ""
	@echo "  Quick start:"
	@echo "    ./$(TARGET) --port 8080 --threads 8 --verbose"
	@echo ""
	@echo "  Stress test:"
	@echo "    python3 tests/stress_test.py"
	@echo "================================================================"
	@echo ""

# ----------------------------------------------------------------------------
# Compilation
# ----------------------------------------------------------------------------

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)

	$(CXX) \
		$(CXXFLAGS) \
		-I$(INC_DIR) \
		-c $< \
		-o $@

# ----------------------------------------------------------------------------
# Debug Build (ASAN + UBSAN)
# ----------------------------------------------------------------------------

.PHONY: debug
debug:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="$(BASE_FLAGS) $(DEBUG_FLAGS)"

# ----------------------------------------------------------------------------
# Release Build
# ----------------------------------------------------------------------------

.PHONY: release
release:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="$(BASE_FLAGS) $(RELEASE_FLAGS)"

# ----------------------------------------------------------------------------
# Clean
# ----------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

	@echo ""
	@echo "Cleaned build artifacts."
	@echo ""

# ----------------------------------------------------------------------------
# Run Targets
# ----------------------------------------------------------------------------

.PHONY: run
run: $(TARGET)
	./$(TARGET)

.PHONY: run-verbose
run-verbose: $(TARGET)
	./$(TARGET) \
		--port 8080 \
		--threads 8 \
		--verbose \
		--stats-interval 5

# ----------------------------------------------------------------------------
# Stress Test
# ----------------------------------------------------------------------------

.PHONY: stress
stress: $(TARGET)

	@echo ""
	@echo "Starting MTCP server..."
	@echo ""

	./$(TARGET) \
		--port 8080 \
		--threads 8 \
		--stats-interval 0 &

	@sleep 1

	@echo ""
	@echo "Running stress test..."
	@echo ""

	@python3 tests/stress_test.py || true

	@echo ""
	@echo "Stopping server..."
	@echo ""

	@pkill -f "./$(TARGET)" 2>/dev/null || true

# ----------------------------------------------------------------------------
# Functional Echo Test
# ----------------------------------------------------------------------------

.PHONY: test-client
test-client:
	python3 tests/echo_client.py 8080

# ----------------------------------------------------------------------------
# Benchmark Script
# ----------------------------------------------------------------------------

.PHONY: benchmark
benchmark:
	chmod +x tests/benchmark.sh
	./tests/benchmark.sh

# ----------------------------------------------------------------------------
# Valgrind (Linux only)
# ----------------------------------------------------------------------------

.PHONY: valgrind
valgrind: $(TARGET)

	@echo ""
	@echo "Valgrind is recommended on Linux."
	@echo ""

	valgrind \
		--leak-check=full \
		--track-origins=yes \
		--show-leak-kinds=all \
		./$(TARGET) \
		--port 8081 \
		--threads 2 \
		--stats-interval 0

# ----------------------------------------------------------------------------
# clang-format
# ----------------------------------------------------------------------------

.PHONY: format
format:
	@find src include \( -name '*.cpp' -o -name '*.h' \) \
	| xargs clang-format -i 2>/dev/null \
	&& echo "Code formatted successfully." \
	|| echo "clang-format not found."

# ----------------------------------------------------------------------------
# Dependency Visualization
# ----------------------------------------------------------------------------

.PHONY: deps
deps:
	@echo ""
	@echo "================================================================"
	@echo "  Source Files"
	@echo "================================================================"

	@for f in $(SOURCES); do \
		echo "  $$f"; \
	done

	@echo ""
	@echo "================================================================"
	@echo "  Object Files"
	@echo "================================================================"

	@for o in $(OBJECTS); do \
		echo "  $$o"; \
	done

	@echo ""

# ----------------------------------------------------------------------------
# Help
# ----------------------------------------------------------------------------

.PHONY: help
help:
	@echo ""
	@echo "================================================================"
	@echo "  MTCP Build System"
	@echo "================================================================"
	@echo ""
	@echo "  Build Targets:"
	@echo "    make            Build release version"
	@echo "    make debug      Build with sanitizers"
	@echo "    make release    Optimized release build"
	@echo "    make clean      Remove build artifacts"
	@echo ""
	@echo "  Run Targets:"
	@echo "    make run"
	@echo "    make run-verbose"
	@echo ""
	@echo "  Testing:"
	@echo "    make stress"
	@echo "    make benchmark"
	@echo "    make test-client"
	@echo ""
	@echo "  Utilities:"
	@echo "    make format"
	@echo "    make deps"
	@echo "    make valgrind"
	@echo ""
	@echo "================================================================"
	@echo ""