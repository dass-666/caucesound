# cs_core: motor de audio y lógica del DAW en C++17 puro, sin dependencias de plataforma.
# Lo comparten la app Android (JNI + Oboe), la futura app iOS (Core Audio) y las pruebas en escritorio.
add_library(cs_core STATIC
    src/Api.cpp
    src/Drums.cpp
    src/Effects.cpp
    src/Engine.cpp
    src/Midi.cpp
    src/Processor.cpp
    src/Project.cpp
    src/Resampler.cpp
    src/Session.cpp
    src/SessionMusic.cpp
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
