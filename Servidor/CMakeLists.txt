cmake_minimum_required(VERSION 3.16)
project(roomba_servidor C)

include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Cliente")
set(CLIENT_DIST_DIR "${CLIENT_DIR}/dist/scrap-e-controller/browser")

FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.19
    GIT_SHALLOW TRUE
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)

find_program(NPM_EXECUTABLE npm)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm es necesario para compilar el cliente Angular")
endif()

add_custom_target(cliente_web
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${CLIENT_DIR}"
    COMMENT "Compilando el cliente Angular"
)

add_library(civetweb STATIC
    third_party/civetweb/src/civetweb.c
)

target_include_directories(civetweb PUBLIC
    third_party/civetweb/include
)

target_compile_definitions(civetweb PRIVATE
    USE_WEBSOCKET
    NO_SSL
    NO_CGI
    NO_LUA
    NO_DUKTAPE
)

target_link_libraries(civetweb PRIVATE pthread)

add_executable(servidor servidor.c)
add_dependencies(servidor cliente_web)

add_executable(logica_simulador ../Logica/logica.c)
target_include_directories(logica_simulador PRIVATE "${cjson_SOURCE_DIR}")
target_link_libraries(logica_simulador PRIVATE cjson)
target_compile_options(logica_simulador PRIVATE -Wall -Wextra -Wpedantic)

target_include_directories(servidor PRIVATE
    third_party/civetweb/include
)

target_link_libraries(servidor PRIVATE civetweb pthread)

target_compile_options(servidor PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

install(TARGETS servidor RUNTIME DESTINATION bin)
install(TARGETS logica_simulador RUNTIME DESTINATION bin)
install(DIRECTORY "${CLIENT_DIST_DIR}/" DESTINATION share/roomba-disco/www)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../Logica/estado.json" DESTINATION share/roomba-disco)
