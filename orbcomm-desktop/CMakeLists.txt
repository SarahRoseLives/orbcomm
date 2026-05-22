cmake_minimum_required(VERSION 3.16)
project(orbcomm-decoder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Options ─────────────────────────────────────────────────────────────────

option(HAS_RTLSDR "Enable RTL-SDR support" ON)

# ─── Dependencies ────────────────────────────────────────────────────────────

# FTXUI (fetched from GitHub — no install needed)
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG        v5.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ftxui)

# RTL-SDR (optional)
if(HAS_RTLSDR)
    find_package(rtlsdr CONFIG QUIET)
    if(rtlsdr_FOUND)
        message(STATUS "RTL-SDR found: ${rtlsdr_DIR}")
    else()
        message(STATUS "RTL-SDR not found — building without live SDR support")
        set(HAS_RTLSDR OFF)
    endif()
endif()

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

# ─── TUI Decoder ────────────────────────────────────────────────────────────

add_executable(orbcomm_tui
    src/tui_main.cpp
)
target_link_libraries(orbcomm_tui PRIVATE orbcomm_dsp ftxui::screen ftxui::dom ftxui::component)
target_compile_options(orbcomm_tui PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /wd4100 /D_CRT_SECURE_NO_WARNINGS>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -O2>
)
if(HAS_RTLSDR)
    target_compile_definitions(orbcomm_tui PRIVATE HAS_RTLSDR)
    target_link_libraries(orbcomm_tui PRIVATE $<IF:$<TARGET_EXISTS:rtlsdr::rtlsdr>,rtlsdr::rtlsdr,rtlsdr::rtlsdr_static>)
endif()

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
