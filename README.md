# CAUCESOUND

DAW (estación de trabajo de audio digital) móvil para Android e iOS. **Fase 1: prototipo Android** sobre un motor
de audio C++ compartido. No es un producto terminado: ver [docs/STATUS.md](docs/STATUS.md) para lo implementado,
lo probado automáticamente y lo que falta validar en un teléfono real.

## Qué incluye la fase 1

- Motor C++17 en tiempo real (`core/`): multipista sincronizado a la muestra, volumen/pan/mute/solo con rampas,
  metrónomo, clips con recorte, fundidos y ganancia, grabación a disco sin bloqueos, exportación WAV idéntica a la
  reproducción, proyectos JSON con guardado atómico y recuperación de grabaciones interrumpidas, interfaz de plugins.
- App Android (`android/`): Oboe (AAudio/OpenSL ES) en dúplex, interfaz Jetpack Compose oscura en español e inglés:
  proyectos, timeline con formas de onda y gestos, mezclador por pista, importación (WAV, MP3, AAC, FLAC… vía el
  sistema), exportación WAV 16/24/32 bits.

## Compilar y probar

```bash
# Motor y pruebas (Linux/macOS)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests

# App Android (requiere SDK 36, NDK 27.2.12479018, CMake 3.31.6)
cd android && ./gradlew assembleDebug testDebugUnitTest
```

Instalar en un Samsung desde Windows: [docs/TESTING_ON_DEVICE.md](docs/TESTING_ON_DEVICE.md).

## Documentación

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): comparación de alternativas (JUCE, nativo, Flutter…) y decisión.
- [docs/PLUGINS.md](docs/PLUGINS.md): VocalShift, Solo AI y Spatial Echo; AUv3 en iOS y opciones en Android.
- [docs/STATUS.md](docs/STATUS.md): estado por requisito con su evidencia.
- [docs/IOS.md](docs/IOS.md): lo pendiente para iPhone.
- [docs/LICENSES.md](docs/LICENSES.md): dependencias, licencias y posibles gastos.
