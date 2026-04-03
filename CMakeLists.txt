cmake_minimum_required(VERSION 3.18)
project(zscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Core library
add_library(zscript_core STATIC
    src/lexer.cpp
    src/token.cpp
    src/parser.cpp
    src/chunk.cpp
    src/compiler.cpp
    src/vm.cpp
)
target_include_directories(zscript_core PUBLIC src)

# CLI driver
add_executable(zsc
    src/main.cpp
)
target_link_libraries(zsc PRIVATE zscript_core)

# Tests
enable_testing()
add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE zscript_core)
add_test(NAME lexer COMMAND test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE zscript_core)
add_test(NAME parser COMMAND test_parser)

add_executable(test_vm tests/test_vm.cpp)
target_link_libraries(test_vm PRIVATE zscript_core)
add_test(NAME vm COMMAND test_vm)
