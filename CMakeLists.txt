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

add_executable(test_closures tests/test_closures.cpp)
target_link_libraries(test_closures PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_closures)

add_executable(test_control_flow tests/test_control_flow.cpp)
target_link_libraries(test_control_flow PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_control_flow)

add_executable(test_collections tests/test_collections.cpp)
target_link_libraries(test_collections PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_collections)

add_executable(test_classes tests/test_classes.cpp)
target_link_libraries(test_classes PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_classes)

add_executable(test_functions tests/test_functions.cpp)
target_link_libraries(test_functions PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_functions)

add_executable(test_operators tests/test_operators.cpp)
target_link_libraries(test_operators PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_operators)

add_executable(test_error_handling tests/test_error_handling.cpp)
target_link_libraries(test_error_handling PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_error_handling)

add_executable(test_stdlib tests/test_stdlib.cpp)
target_link_libraries(test_stdlib PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_stdlib)

add_executable(test_gc_module tests/test_gc_module.cpp)
target_link_libraries(test_gc_module PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_gc_module)

add_executable(test_hotpatch tests/test_hotpatch.cpp)
target_link_libraries(test_hotpatch PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_hotpatch)

add_executable(test_io_os tests/test_io_os.cpp)
target_link_libraries(test_io_os PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_io_os)

add_executable(test_delegates tests/test_delegates.cpp)
target_link_libraries(test_delegates PRIVATE zscript_core Catch2::Catch2WithMain)
catch_discover_tests(test_delegates)
