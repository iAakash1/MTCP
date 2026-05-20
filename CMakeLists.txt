cmake_minimum_required(VERSION 3.14)
project(MTCP VERSION 2.0.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings & optimization ──────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -O2
)

# ── Collect all source files ───────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    src/*.cpp
)

# ── Server executable ──────────────────────────────────────────────────────────
add_executable(server ${SOURCES})

target_include_directories(server PRIVATE include)
target_link_libraries(server PRIVATE pthread)

# ── Optional: AddressSanitizer build ──────────────────────────────────────────
# cmake -DASAN=ON ..
option(ASAN "Build with AddressSanitizer" OFF)
if(ASAN)
    target_compile_options(server PRIVATE -fsanitize=address -g)
    target_link_options(server PRIVATE -fsanitize=address)
    message(STATUS "AddressSanitizer enabled")
endif()

# ── Optional: ThreadSanitizer build ───────────────────────────────────────────
# cmake -DTSAN=ON ..
option(TSAN "Build with ThreadSanitizer" OFF)
if(TSAN)
    target_compile_options(server PRIVATE -fsanitize=thread -g)
    target_link_options(server PRIVATE -fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled")
endif()

message(STATUS "MTCP v${PROJECT_VERSION} — build ready")
message(STATUS "  Run: cmake --build . && ./server --help")
