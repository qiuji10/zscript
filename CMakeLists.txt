cmake_minimum_required(VERSION 3.18)

# ---------------------------------------------------------------------------
# Fuzzer option must be declared BEFORE project() so the CRT choice below
# takes effect for ALL targets (including zscript_core).
# ---------------------------------------------------------------------------
option(ZSCRIPT_FUZZ "Build libFuzzer targets (requires clang -fsanitize=fuzzer)" OFF)

# On Windows, clang_rt.fuzzer is compiled with static CRT (/MT).
# Every target in the build must use the same CRT or lld-link will error.
# Setting CMAKE_MSVC_RUNTIME_LIBRARY here covers zscript_core and all tests.
if(ZSCRIPT_FUZZ AND WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(zscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Catch2 via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# Core library
add_library(zscript_core STATIC
    src/lexer.cpp
    src/token.cpp
    src/parser.cpp
    src/chunk.cpp
    src/compiler.cpp
    src/vm.cpp
    src/gc.cpp
    src/module.cpp
    src/filewatcher.cpp
    src/hotpatch.cpp
    src/lsp.cpp
    src/dap.cpp
)
target_include_directories(zscript_core PUBLIC src)

# macOS: link CoreServices for FSEvents
if(APPLE)
    target_link_libraries(zscript_core PUBLIC "-framework CoreServices")
endif()

# ---------------------------------------------------------------------------
# Unity C API shared library (zscript_unity)
# ---------------------------------------------------------------------------
option(ZSCRIPT_UNITY "Build the Unity plugin shared library" OFF)

if(ZSCRIPT_UNITY)
    add_library(zscript_unity SHARED
        src/unity/zscript_c.cpp
    )
    target_include_directories(zscript_unity PUBLIC src src/unity)
    target_link_libraries(zscript_unity PRIVATE zscript_core)

    # Export only the public zs_* C symbols; hide all internal C++ symbols.
    set_target_properties(zscript_unity PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # ---------------------------------------------------------------------------
    # Platform-specific output name / suffix and Unity Plugins/ install path
    # ---------------------------------------------------------------------------
    set(UNITY_PLUGINS_DIR "${CMAKE_SOURCE_DIR}/unity/com.zscript.runtime/Plugins" CACHE PATH
        "Root of the Unity project's Plugins/ folder")

    if(WIN32)
        set_target_properties(zscript_unity PROPERTIES OUTPUT_NAME "zscript_unity")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_unity_dest "${UNITY_PLUGINS_DIR}/Windows/x86_64")
        else()
            set(_unity_dest "${UNITY_PLUGINS_DIR}/Windows/x86")
        endif()
    elseif(APPLE)
        # Unity editor on macOS expects .bundle; player builds use .dylib.
        # Default to .bundle (editor workflow); override SUFFIX externally for players.
        set_target_properties(zscript_unity PROPERTIES
            OUTPUT_NAME "zscript_unity"
            SUFFIX      ".bundle"
        )
        set(_unity_dest "${UNITY_PLUGINS_DIR}/macOS")
    elseif(ANDROID)
        set_target_properties(zscript_unity PROPERTIES OUTPUT_NAME "zscript_unity")
        # Android ABI is set by the NDK toolchain (e.g. arm64-v8a, x86_64).
        set(_unity_dest "${UNITY_PLUGINS_DIR}/Android/libs/${ANDROID_ABI}")
    elseif(IOS)
        # iOS requires a static library (App Store forbids dynamic loading).
        set_target_properties(zscript_unity PROPERTIES
            OUTPUT_NAME "zscript_unity"
        )
        set(_unity_dest "${UNITY_PLUGINS_DIR}/iOS")
    else()
        # Linux
        set_target_properties(zscript_unity PROPERTIES OUTPUT_NAME "zscript_unity")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_unity_dest "${UNITY_PLUGINS_DIR}/Linux/x86_64")
        else()
            set(_unity_dest "${UNITY_PLUGINS_DIR}/Linux/x86")
        endif()
    endif()

    # Install rule: `cmake --install build` copies the built library to the
    # Unity project's Plugins/ folder automatically.
    install(TARGETS zscript_unity
        LIBRARY  DESTINATION "${_unity_dest}"
        RUNTIME  DESTINATION "${_unity_dest}"  # .dll on Windows
    )
    # Public header — useful when embedding the C API in native Unity plugins.
    install(FILES src/unity/zscript_c.h
        DESTINATION "${UNITY_PLUGINS_DIR}/../include"
    )
endif()

# CLI driver
add_executable(zsc
    src/main.cpp
)
target_link_libraries(zsc PRIVATE zscript_core)

# Tests
enable_testing()
include(Catch)

set(ZSCRIPT_TEST_RNG_SEED "" CACHE STRING "Catch2 random seed to pass to tests")

function(zscript_discover_tests target)
    if(ZSCRIPT_TEST_RNG_SEED)
        catch_discover_tests(${target} EXTRA_ARGS --rng-seed ${ZSCRIPT_TEST_RNG_SEED})
    else()
        catch_discover_tests(${target})
    endif()
endfunction()

add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_parser)

add_executable(test_vm tests/test_vm.cpp)
target_link_libraries(test_vm PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_vm)

add_executable(test_closures tests/test_closures.cpp)
target_link_libraries(test_closures PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_closures)

add_executable(test_control_flow tests/test_control_flow.cpp)
target_link_libraries(test_control_flow PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_control_flow)

add_executable(test_collections tests/test_collections.cpp)
target_link_libraries(test_collections PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_collections)

add_executable(test_classes tests/test_classes.cpp)
target_link_libraries(test_classes PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_classes)

add_executable(test_functions tests/test_functions.cpp)
target_link_libraries(test_functions PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_functions)

add_executable(test_operators tests/test_operators.cpp)
target_link_libraries(test_operators PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_operators)

add_executable(test_error_handling tests/test_error_handling.cpp)
target_link_libraries(test_error_handling PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_error_handling)

add_executable(test_stdlib tests/test_stdlib.cpp)
target_link_libraries(test_stdlib PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_stdlib)

add_executable(test_gc_module tests/test_gc_module.cpp)
target_link_libraries(test_gc_module PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_gc_module)

add_executable(test_hotpatch tests/test_hotpatch.cpp)
target_link_libraries(test_hotpatch PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_hotpatch)

add_executable(test_io_os tests/test_io_os.cpp)
target_link_libraries(test_io_os PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_io_os)

add_executable(test_delegates tests/test_delegates.cpp)
target_link_libraries(test_delegates PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_delegates)

add_executable(test_generics tests/test_generics.cpp)
target_link_libraries(test_generics PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_generics)

add_executable(test_tags tests/test_tags.cpp)
target_link_libraries(test_tags PRIVATE zscript_core Catch2::Catch2WithMain)
zscript_discover_tests(test_tags)

add_executable(test_c_api tests/test_c_api.cpp src/unity/zscript_c.cpp)
target_include_directories(test_c_api PRIVATE src src/unity)
target_link_libraries(test_c_api PRIVATE zscript_core Catch2::Catch2WithMain)
if(APPLE)
    target_link_libraries(test_c_api PRIVATE "-framework CoreServices")
endif()
zscript_discover_tests(test_c_api)

# ---------------------------------------------------------------------------
# Fuzzer (libFuzzer) — opt-in via -DZSCRIPT_FUZZ=ON, requires clang.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Fuzzer (libFuzzer) — opt-in via -DZSCRIPT_FUZZ=ON.
# Requires clang on any platform: clang++ on Linux/macOS, or the LLVM clang++
# that ships with Visual Studio / LLVM installer on Windows.
# NOTE: option(ZSCRIPT_FUZZ) is declared at the top of this file so the
#       Windows static-CRT workaround applies to all targets.
# ---------------------------------------------------------------------------
if(ZSCRIPT_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "ZSCRIPT_FUZZ requires a Clang compiler.\n"
            "  Linux/macOS : -DCMAKE_CXX_COMPILER=clang++\n"
            "  Windows     : -DCMAKE_CXX_COMPILER=\"C:/Program Files/LLVM/bin/clang++.exe\"\n"
            "  (got: ${CMAKE_CXX_COMPILER_ID})")
    endif()
    # zscript_core must also be compiled with ASAN so the annotate_string
    # /failifmismatch flag agrees between the library and the fuzzer object.
    target_compile_options(zscript_core PRIVATE -fsanitize=address)

    add_executable(fuzz_lexer_parser fuzz/fuzz_lexer_parser.cpp)
    target_link_libraries(fuzz_lexer_parser PRIVATE zscript_core)
    # Use -fsanitize=fuzzer,address on all platforms.
    # On Windows, Clang 20+ pulls in clang_rt.ubsan_standalone as a transitive
    # dependency of the fuzzer runtime; ubsan_standalone needs __coe_win symbols
    # and dbghelp.lib, both of which are satisfied by linking ASAN.
    # GitHub's Windows runners ship VS2022 with full ASAN support.
    target_compile_options(fuzz_lexer_parser PRIVATE -fsanitize=fuzzer,address -g)
    target_link_options   (fuzz_lexer_parser PRIVATE -fsanitize=fuzzer,address)
    message(STATUS "Fuzzer target: fuzz_lexer_parser")
endif()
