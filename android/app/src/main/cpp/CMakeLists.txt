cmake_minimum_required(VERSION 3.22)
project(caucesound_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Núcleo compartido con iOS y las pruebas de escritorio (raíz del repositorio /core)
set(CS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
add_subdirectory(${CS_ROOT}/core ${CMAKE_BINARY_DIR}/cs_core)

find_package(oboe REQUIRED CONFIG)

add_library(caucesound SHARED
    AudioIO.cpp
    JniBridge.cpp
)
target_link_libraries(caucesound PRIVATE cs_core oboe::oboe log android)
target_compile_options(caucesound PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Páginas de 16 KB (obligatorio para nuevas apps en Google Play desde noviembre de 2025)
target_link_options(caucesound PRIVATE "-Wl,-z,max-page-size=16384")
