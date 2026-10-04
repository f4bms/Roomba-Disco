cmake_minimum_required(VERSION 3.13)
project(roombateca C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

option(ROOMBATECA_HARDWARE "Compilar soporte GPIO real" ON)
option(ROOMBATECA_SIM_INJECT "Usar sensores inyectables via ROO_SIM_SENSORS_FILE (solo simulacion)" OFF)

set(ROOMBATECA_SOURCES
    lib/leds.c
    lib/audio_th.c
    lib/succion.c
)

if(ROOMBATECA_HARDWARE)
    list(APPEND ROOMBATECA_SOURCES
        lib/gpio_control.c
        lib/pwm_control.c
        lib/motores.c
        lib/sensores.c
        lib/encoders.c
    )
elseif(ROOMBATECA_SIM_INJECT)
    list(APPEND ROOMBATECA_SOURCES
        lib/sim/motores.c
        lib/sensores_inyectables.c
        lib/sim/encoders.c
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

if(ROOMBATECA_HARDWARE)
    target_compile_definitions(roombateca PRIVATE ROOMBATECA_HARDWARE)
endif()

if(DEFINED ROOMBATECA_AUDIO_DIR)
    target_compile_definitions(roombateca PRIVATE "AUDIO_DIR=\"${ROOMBATECA_AUDIO_DIR}/\"")
endif()

target_link_libraries(roombateca PRIVATE Threads::Threads)

if(ROOMBATECA_HARDWARE)
    target_link_libraries(roombateca PRIVATE gpiod)
endif()

install(TARGETS roombateca LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
