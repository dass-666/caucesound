cmake_minimum_required(VERSION 3.22)
project(CauceSound VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CS_BUILD_TESTS "Build core tests" ON)
option(CS_SANITIZE "Build with ASan + UBSan" OFF)
if (CS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

# Herramienta de escritorio sin interfaz (primer paso de CauceSound Studio / Cauce Link). Ver desktop/cli/main.cpp.
option(CS_BUILD_DESKTOP "Compilar la herramienta de escritorio cauce" ON)
if (CS_BUILD_DESKTOP)
    add_executable(cauce desktop/cli/main.cpp)
    target_link_libraries(cauce PRIVATE cs_core)
    # Constructor reproducible de los paquetes Cauce Signature Sounds (misión 0.9).
    add_executable(cauce_packs desktop/tools/packbuilder/PackBuilder.cpp)
    target_link_libraries(cauce_packs PRIVATE cs_core)
endif()

# Hotfix 0.9.2: el puente JNI real (JniBridge.cpp) compilado para el ordenador, con un AudioIO sin Oboe. Lo cargan
# las pruebas Robolectric de extremo a extremo (interfaz -> DawViewModel -> JNI -> motor -> estado). No va en el APK.
option(CS_BUILD_HOST_JNI "Compilar libcaucesound para la JVM de escritorio (pruebas de la app)" OFF)
if (CS_BUILD_HOST_JNI)
    find_package(JNI REQUIRED)
    add_library(caucesound SHARED android/app/src/main/cpp/JniBridge.cpp)
    target_include_directories(caucesound PRIVATE ${JNI_INCLUDE_DIRS} android/app/src/test/cpp)
    target_compile_definitions(caucesound PRIVATE CS_HOST_JNI=1)
    target_link_libraries(caucesound PRIVATE cs_core)
endif()

if (CS_BUILD_TESTS)
    enable_testing()
    add_executable(cs_core_tests core/tests/TestMain.cpp core/tests/TestCore.cpp core/tests/TestSession.cpp core/tests/TestMusic.cpp core/tests/TestEffects.cpp core/tests/TestStress.cpp core/tests/TestArchive.cpp core/tests/TestQuality.cpp core/tests/TestFuzz.cpp core/tests/TestBus.cpp core/tests/TestSwitch.cpp core/tests/TestComposer.cpp core/tests/TestSolo.cpp core/tests/TestVocal.cpp core/tests/TestExport.cpp core/tests/TestSoloAlternatives.cpp core/tests/TestMixerPro.cpp core/tests/TestChannelRack.cpp core/tests/TestSampler.cpp core/tests/TestVariations.cpp core/tests/TestVocalLive.cpp core/tests/TestSignature2.cpp)
    target_link_libraries(cs_core_tests PRIVATE cs_core)
    add_test(NAME cs_core_tests COMMAND cs_core_tests)
    add_executable(cs_bench core/tests/Bench.cpp)
    target_link_libraries(cs_bench PRIVATE cs_core)
    add_executable(cs_render core/tests/Render.cpp)
    target_link_libraries(cs_render PRIVATE cs_core)
endif()
