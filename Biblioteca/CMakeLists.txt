cmake_minimum_required(VERSION 3.13)
project(roombateca C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

option(ROOMBATECA_HARDWARE "Compilar soporte GPIO real" ON)

set(ROOMBATECA_SOURCES
    lib/leds.c
    lib/audio_th.c
    lib/roombateca.c
)

if(ROOMBATECA_HARDWARE)
    list(APPEND ROOMBATECA_SOURCES
        lib/gpio_control.c
        lib/pwm_control.c
        lib/motores.c
        lib/sensores.c
        lib/encoders.c
    )
else()
    list(APPEND ROOMBATECA_SOURCES
        lib/sim/motores.c
        lib/sim/sensores.c
        lib/sim/encoders.c
    )
endif()

add_library(roombateca SHARED
    ${ROOMBATECA_SOURCES}
)

target_include_directories(roombateca PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(roombateca PRIVATE Threads::Threads)

if(ROOMBATECA_HARDWARE)
    target_link_libraries(roombateca PRIVATE gpiod)
endif()

install(TARGETS roombateca LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
