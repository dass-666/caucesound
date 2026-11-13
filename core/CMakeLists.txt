# cs_core: motor de audio y lógica del DAW en C++17 puro, sin dependencias de plataforma.
# Lo comparten la app Android (JNI + Oboe), la futura app iOS (Core Audio) y las pruebas en escritorio.
add_library(cs_core STATIC
    src/Api.cpp
    src/Archive.cpp
    src/Drums.cpp
    src/Effects.cpp
    src/FxChains.cpp
    src/Engine.cpp
    src/Midi.cpp
    src/Smf.cpp
    src/Hash.cpp
    src/SessionExport.cpp
    src/Processor.cpp
    src/Project.cpp
    src/Resampler.cpp
    src/Session.cpp
    src/SessionMusic.cpp
    src/Composer.cpp
    src/SessionSolo.cpp
    src/SoloProfiles.cpp
    src/SessionVocal.cpp
    src/Jobs.cpp
    src/VocalBridge.cpp
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

# --- VocalShift (submódulo fijado en third_party/vocalshift-ai-studio, rama feat/urban-spatial @ 10e679c) ----------
# Solo su núcleo vs_core (C++17, sin JUCE). No se usa su CMake raíz (descargaría JUCE). Signalsmith Stretch y Linear
# (MIT, solo cabeceras) van como submódulos fijados a los mismos commits que declara VocalShift.
set(CS_VS_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/vocalshift-ai-studio)
set(CS_SS_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/signalsmith-stretch)
set(CS_SL_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/signalsmith-linear)
option(CS_WITH_VOCALSHIFT "Integrar el núcleo de VocalShift (procesado vocal offline)" ON)
if (CS_WITH_VOCALSHIFT AND EXISTS ${CS_VS_DIR}/src/core/VocalChain.cpp AND EXISTS ${CS_SS_DIR}/include AND EXISTS ${CS_SL_DIR}/include)
    file(GLOB_RECURSE CS_VS_SOURCES ${CS_VS_DIR}/src/core/*.cpp)
    add_library(cs_vocalshift STATIC ${CS_VS_SOURCES})
    target_include_directories(cs_vocalshift PUBLIC ${CS_VS_DIR}/src ${CS_SS_DIR}/include ${CS_SL_DIR}/include)
    target_compile_features(cs_vocalshift PUBLIC cxx_std_17)
    if (NOT MSVC)
        # Código externo sin avisos; Signalsmith es muy lento sin optimizar, así que -O2 también en Debug.
        target_compile_options(cs_vocalshift PRIVATE -w $<$<CONFIG:Debug>:-O2>)
    endif()
    target_link_libraries(cs_core PUBLIC cs_vocalshift)
    target_compile_definitions(cs_core PUBLIC CS_WITH_VOCALSHIFT=1)
    message(STATUS "CAUCESOUND: VocalShift integrado desde ${CS_VS_DIR}")
else()
    message(STATUS "CAUCESOUND: VocalShift no disponible (faltan submódulos en third_party)")
endif()
