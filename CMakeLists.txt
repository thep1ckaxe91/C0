cmake_minimum_required(VERSION 3.16)

project("c0c" VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -O2 -g)

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
    src/parser/driver.cpp
    ${BISON_C0Parser_OUTPUTS}
    ${FLEX_C0Lexer_OUTPUTS}
)

add_executable(c0c ${SOURCES})
