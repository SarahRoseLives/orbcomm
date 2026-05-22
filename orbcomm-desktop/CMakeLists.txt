cmake_minimum_required(VERSION 3.16)
project(orbcomm-decoder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── DSP Library (header-only) ──────────────────────────────────────────────

add_library(orbcomm_dsp INTERFACE)
target_include_directories(orbcomm_dsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
)
target_compile_features(orbcomm_dsp INTERFACE cxx_std_17)

# ─── CLI Decoder ────────────────────────────────────────────────────────────

add_executable(orbcomm_cli
    src/main.cpp
)
target_link_libraries(orbcomm_cli PRIVATE orbcomm_dsp)
target_compile_options(orbcomm_cli PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -O2>
)

# ─── Tests ──────────────────────────────────────────────────────────────────

enable_testing()
add_executable(orbcomm_test
    test/test_decoder.cpp
)
target_link_libraries(orbcomm_test PRIVATE orbcomm_dsp)
target_compile_options(orbcomm_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -O2>
)

add_test(NAME orbcomm_unit COMMAND orbcomm_test)
