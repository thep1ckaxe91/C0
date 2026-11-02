cmake_minimum_required(VERSION 3.16)

project("c0c" VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-fno-omit-frame-pointer)
endif()

find_package(BISON 3.0 REQUIRED)
find_package(FLEX 2.6 REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

bison_target(
    C0Parser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/parser.y
    ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.cpp
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.hpp
)

flex_target(
    C0Lexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/lexer.l
    ${CMAKE_CURRENT_BINARY_DIR}/lexer.yy.cpp
)

add_flex_bison_dependency(C0Lexer C0Parser)

set(SOURCES
    src/main.cpp
    src/ast/ast.cpp
    src/memory/arena.cpp
    src/parser/driver.cpp
    src/typecheck/typechecker.cpp
    ${BISON_C0Parser_OUTPUTS}
    ${FLEX_C0Lexer_OUTPUTS}
)

add_executable(c0c ${SOURCES})

add_executable(test_arena tests/test_arena.cpp src/memory/arena.cpp)

