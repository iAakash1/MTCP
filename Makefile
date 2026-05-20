# ══════════════════════════════════════════════════════════════════════════════
# Makefile — MTCP Multithreaded TCP Server v2
# ══════════════════════════════════════════════════════════════════════════════

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
LDFLAGS  := -pthread

SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := obj
TARGET   := server

# Recursively find all .cpp files under src/
SOURCES  := $(shell find $(SRC_DIR) -name '*.cpp')

# Mirror the src/ subdirectory structure inside obj/
# e.g.  src/util/Logger.cpp  →  obj/util/Logger.o
OBJECTS  := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SOURCES))

# ── Default target ─────────────────────────────────────────────────────────────
.PHONY: all
all: $(TARGET)

# ── Link ───────────────────────────────────────────────────────────────────────
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	@echo ""
	@echo "  ✅ Build successful → ./$(TARGET)"
	@echo "  Run:   ./$(TARGET) --help"
	@echo "  Quick: ./$(TARGET) --port 8080 --threads 4 --verbose"
	@echo ""

# ── Compile (create obj subdirectory as needed) ────────────────────────────────
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

# ── Clean ──────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "🧹 Cleaned build artifacts."

# ── Run with defaults ──────────────────────────────────────────────────────────
.PHONY: run
run: $(TARGET)
	./$(TARGET)

# ── Run with verbose output ────────────────────────────────────────────────────
.PHONY: run-verbose
run-verbose: $(TARGET)
	./$(TARGET) --verbose --stats-interval 5

# ── Stress test (starts server, runs Python test, kills server) ────────────────
.PHONY: stress
stress: $(TARGET)
	@echo "Starting server in background..."
	./$(TARGET) --port 8080 --threads 8 --stats-interval 0 &
	@sleep 0.5
	@python3 tests/stress_test.py || true
	@pkill -f "./$(TARGET)" 2>/dev/null || true

# ── Single-client functional test ─────────────────────────────────────────────
.PHONY: test-client
test-client:
	python3 tests/echo_client.py 8080

# ── Valgrind memory-leak check ─────────────────────────────────────────────────
.PHONY: valgrind
valgrind: $(TARGET)
	valgrind \
	  --leak-check=full \
	  --track-origins=yes \
	  --show-leak-kinds=all \
	  ./$(TARGET) --port 8081 --threads 2 --stats-interval 0

# ── clang-format (if available) ────────────────────────────────────────────────
.PHONY: format
format:
	@find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -i 2>/dev/null \
	  && echo "✅ Formatted" || echo "⚠️  clang-format not found"

# ── Show build dependency graph ────────────────────────────────────────────────
.PHONY: deps
deps:
	@echo "Sources:"
	@for f in $(SOURCES); do echo "  $$f"; done
	@echo "Objects:"
	@for o in $(OBJECTS); do echo "  $$o"; done
