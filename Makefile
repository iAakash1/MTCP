# ══════════════════════════════════════════════════════════════════════════════
# Makefile — Multithreaded TCP Server
# ══════════════════════════════════════════════════════════════════════════════

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
LDFLAGS  := -pthread

SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := obj

SOURCES  := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS  := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SOURCES))
TARGET   := server

# ── Default target ────────────────────────────────────────────────────────────
all: $(TARGET)

# ── Link ──────────────────────────────────────────────────────────────────────
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	@echo "✅ Build successful: ./$(TARGET)"

# ── Compile ───────────────────────────────────────────────────────────────────
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

# ── Create obj directory ──────────────────────────────────────────────────────
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "🧹 Cleaned build artifacts."

# ── Run ───────────────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

# ── Phony targets ─────────────────────────────────────────────────────────────
.PHONY: all clean run
