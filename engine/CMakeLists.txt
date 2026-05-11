cmake_minimum_required(VERSION 3.13)
project(yatsi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(yatsi
    src/yatsi.cpp
    src/evaluate.cpp
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
)
