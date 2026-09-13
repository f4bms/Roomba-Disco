cmake_minimum_required(VERSION 3.13)
project(roombateca C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(roombateca SHARED
    lib/motores.c
    lib/sensores.c
    lib/leds.c
    lib/audio_th.c
    lib/roombateca.c
)

target_include_directories(roombateca PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(roombateca PRIVATE Threads::Threads)

install(TARGETS roombateca LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
