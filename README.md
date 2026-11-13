# CAUCESOUND

DAW (estación de trabajo de audio digital) móvil para Android e iOS. **Fase 1.2: prototipo Android** con beatmaker,
MIDI, sintetizador y efectos propios sobre un motor de audio C++ compartido. No es un producto terminado: ver [docs/STATUS.md](docs/STATUS.md) para lo implementado,
lo probado automáticamente y lo que falta validar en un teléfono real.

## Qué incluye la fase 1

- Motor C++17 en tiempo real (`core/`): multipista sincronizado a la muestra, volumen/pan/mute/solo con rampas,
  metrónomo, clips con recorte, fundidos y ganancia, grabación a disco sin bloqueos, exportación WAV idéntica a la
  reproducción, proyectos JSON con guardado atómico y recuperación de grabaciones interrumpidas, interfaz de plugins.
- App Android (`android/`): Oboe (AAudio/OpenSL ES) en dúplex, interfaz Jetpack Compose oscura en español e inglés:
  proyectos, timeline con formas de onda y gestos, mezclador por pista, importación (WAV, MP3, AAC, FLAC… vía el
  sistema), exportación WAV 16/24/32 bits.

## Qué añade la fase 1.2

- Núcleo MIDI exacto a la muestra (notas, clips, cuantización, swing, bucle), **Cauce Drums** (secuenciador de 16/32
  pasos y pads, 4 kits sintetizados), **Cauce Synth** (7 presets), **Cauce FX** (filtro, EQ, saturación, compresor,
  delay, reverb) y plantillas Trap/Reggaetón/Hip-hop/Pop/Electrónica con contenido real.
- Interfaz rediseñada: inicio con plantillas y proyectos recientes, estudio con Arreglo, Batería, Piano roll, Synth,
  Mezclador y FX. Proyectos v2 compatibles con los de la fase 1.

## Compilar y probar

```bash
# Motor y pruebas (Linux/macOS)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests

# App Android (requiere SDK 36, NDK 27.2.12479018, CMake 3.31.6)
cd android && ./gradlew assembleDebug testDebugUnitTest      # -Pscreenshots guarda capturas renderizadas (JVM)
# APK de prueba instalable junto a la fase 1: CAUCE_SIGNING=/ruta/keystore.properties ./gradlew assemblePreview
```

Instalar en un Samsung desde Windows: [docs/TESTING_ON_DEVICE.md](docs/TESTING_ON_DEVICE.md).

## Documentación

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): comparación de alternativas (JUCE, nativo, Flutter…) y decisión.
- [docs/PLUGINS.md](docs/PLUGINS.md): VocalShift, Solo AI y Spatial Echo; AUv3 en iOS y opciones en Android.
- [docs/STATUS.md](docs/STATUS.md): estado por requisito con su evidencia.
- [docs/IOS.md](docs/IOS.md): lo pendiente para iPhone.
- [docs/LICENSES.md](docs/LICENSES.md): dependencias, licencias, origen del contenido musical y posibles gastos.
- [docs/NEXT_SESSION.md](docs/NEXT_SESSION.md): estado para retomar el trabajo y decisiones pendientes.
