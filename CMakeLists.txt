cmake_minimum_required(VERSION 3.18)
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

# CLI driver
add_executable(zsc
    src/main.cpp
)
target_link_libraries(zsc PRIVATE zscript_core)

# Tests
enable_testing()
include(Catch)

add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_parser)

add_executable(test_vm tests/test_vm.cpp)
target_link_libraries(test_vm PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_vm)

add_executable(test_phase3 tests/test_phase3.cpp)
target_link_libraries(test_phase3 PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_phase3)

add_executable(test_phase4 tests/test_phase4.cpp)
target_link_libraries(test_phase4 PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_phase4)
