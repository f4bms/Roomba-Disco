cmake_minimum_required(VERSION 3.16)
project(roomba_servidor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(servidor servidor.c)

target_compile_options(servidor PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
