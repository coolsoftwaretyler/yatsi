cmake_minimum_required(VERSION 3.13)
project(yatsi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

set(YATSI_CORE_SOURCES
    src/evaluate.cpp
    src/common/value.cpp
    src/lexer/token.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/parser/ast_printer.cpp
    src/compiler/bytecode.cpp
    src/compiler/compiler.cpp
    src/compiler/disassembler.cpp
    src/runtime/gc.cpp
    src/runtime/js_string.cpp
    src/runtime/js_function.cpp
    src/vm/vm.cpp
)

if(EMSCRIPTEN)
    add_executable(yatsi
        ${YATSI_CORE_SOURCES}
        src/yatsi.cpp
        src/wasm_api.cpp
    )
    target_include_directories(yatsi PRIVATE src)
    set_target_properties(yatsi PROPERTIES
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    target_link_options(yatsi PRIVATE
        -lembind
        -sMODULARIZE=1
        -sEXPORT_NAME=createYatsi
        -sEXPORT_ES6=1
        -sENVIRONMENT=web,worker
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=16777216
        -sFILESYSTEM=0
        -sNO_EXIT_RUNTIME=1
        -sSTACK_SIZE=1048576
        -sDISABLE_EXCEPTION_CATCHING=0
    )
else()
    add_executable(yatsi_cli
        ${YATSI_CORE_SOURCES}
        src/main.cpp
    )
    target_include_directories(yatsi_cli PRIVATE src)
endif()
