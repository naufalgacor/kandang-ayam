cmake_minimum_required(VERSION 3.10)
project(SimulasiPeternakan)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ── Temukan paket ──────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# assimp opsional (kalau tidak ada, kita tidak pakai)
find_package(assimp QUIET)

# ── Source files ───────────────────────────────────────────
add_executable(SimulasiPeternakan
    src/main.cpp
    src/glad.c
    src/stb_image.cpp
)

# ── Include dirs ───────────────────────────────────────────
target_include_directories(SimulasiPeternakan PRIVATE src)

# ── Link libraries ─────────────────────────────────────────
target_link_libraries(SimulasiPeternakan
    glfw
    OpenGL::GL
)

if(assimp_FOUND)
    target_link_libraries(SimulasiPeternakan assimp)
    target_compile_definitions(SimulasiPeternakan PRIVATE USE_ASSIMP)
endif()

# Math library (dibutuhkan di Linux)
if(UNIX)
    target_link_libraries(SimulasiPeternakan m)
endif()

# ── Copy shader ke build dir (opsional, kalau masih dipakai) ──
# (shader sekarang embedded di main.cpp sebagai raw string literal)

message(STATUS ">> Konfigurasi berhasil. Jalankan: cmake --build .")
