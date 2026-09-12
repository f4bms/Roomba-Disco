cmake_minimum_required(VERSION 3.10)
project(libthgpio VERSION 1.0.0 LANGUAGES C)

# Rutas headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Busca paquete nativo de hilos POSIX (Mapea -lpthread automáticamente)
find_package(Threads REQUIRED)

# 1. Construir la biblioteca compartida
# Compila como Shared Object (.so) e inyecta banderas necesarias
add_library(thgpio SHARED lib/thgpio.c)
target_link_libraries(thgpio PRIVATE Threads::Threads)

# 2. Construir el ejecutable de prueba enlazado a tu biblioteca
add_executable(test_car main_test.c)
target_link_libraries(test_car PRIVATE thgpio Threads::Threads)

# Reglas de instalación necesarias para Yocto
install(TARGETS thgpio DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
