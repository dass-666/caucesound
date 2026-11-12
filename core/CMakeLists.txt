# cs_core: motor de audio y lógica del DAW en C++17 puro, sin dependencias de plataforma.
# Lo comparten la app Android (JNI + Oboe), la futura app iOS (Core Audio) y las pruebas en escritorio.
add_library(cs_core STATIC
    src/Api.cpp
    src/Archive.cpp
    src/Drums.cpp
    src/Effects.cpp
    src/Engine.cpp
    src/Midi.cpp
    src/Processor.cpp
    src/Project.cpp
    src/Resampler.cpp
    src/Session.cpp
    src/SessionMusic.cpp
    src/Composer.cpp
    src/SessionSolo.cpp
    src/Synth.cpp
    src/Wav.cpp
)
target_include_directories(cs_core PUBLIC include ${CMAKE_CURRENT_LIST_DIR}/../third_party)
target_compile_features(cs_core PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(cs_core PUBLIC Threads::Threads)
if (MSVC)
    target_compile_options(cs_core PRIVATE /W4)
else()
    target_compile_options(cs_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Solo AI (submódulo fijado en third_party/solo-ai-studio, decisión del propietario 2026-11-12) ----------------
# Solo se compila su núcleo C++17 (solo_core): NO se usa su CMakeLists raíz, que descargaría JUCE para el plugin.
set(CS_SOLO_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/solo-ai-studio)
option(CS_WITH_SOLO "Integrar el núcleo de Solo AI (generación de melodías)" ON)
if (CS_WITH_SOLO AND EXISTS ${CS_SOLO_DIR}/core/src/MelodyExplorer.cpp)
    file(GLOB CS_SOLO_SOURCES ${CS_SOLO_DIR}/core/src/*.cpp)
    add_library(cs_solo STATIC ${CS_SOLO_SOURCES})
    target_include_directories(cs_solo PUBLIC ${CS_SOLO_DIR}/core/include)
    target_compile_features(cs_solo PUBLIC cxx_std_17)
    if (NOT MSVC)
        target_compile_options(cs_solo PRIVATE -w) # código externo: sus avisos no son nuestros
    endif()
    target_sources(cs_core PRIVATE src/SoloBridge.cpp)
    target_link_libraries(cs_core PUBLIC cs_solo)
    target_compile_definitions(cs_core PUBLIC CS_WITH_SOLO=1)
    message(STATUS "CAUCESOUND: Solo AI integrado desde ${CS_SOLO_DIR}")
else()
    target_sources(cs_core PRIVATE src/SoloBridge.cpp) # devuelve «no disponible»
    message(STATUS "CAUCESOUND: Solo AI no disponible (falta el submódulo third_party/solo-ai-studio)")
endif()
