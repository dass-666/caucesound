# Para retomar el trabajo (actualizado 2026-11-14, hotfix 0.9.2)

## Hotfix 0.9.2 (lo más reciente)

- **Rama**: `claude/project-thread-sol38i`, basada en la 0.9.1 (`claude/project-thread-d4hr9u`, PR #7). PR borrador nuevo contra la rama de la 0.9.1. **Nada fusionado.**
- **APK**: `/mnt/project-files/caucesound/caucesound-0.9.2-hotfix-preview.apk`, versionCode 14, misma firma, con paquetes (`CAUCE_PACKS`: descomprimir los 3 ZIP de `paquetes-0.9/`, sin el piano, en `<dir>/packs/<id>/` sin el sufijo de versión).
- **Guía física**: SAMSUNG_ACCEPTANCE_0.9.2.md (14 pruebas). Nada probado en teléfono.
- **Pruebas de extremo a extremo en la JVM** con el `JniBridge.cpp` real: `cmake -S . -B build-hostjni -DCS_BUILD_HOST_JNI=ON -DCS_BUILD_TESTS=OFF && cmake --build build-hostjni --target caucesound`, luego `CS_HOST_JNI_DIR=$PWD/build-hostjni ./gradlew testDebugUnitTest`. Sin la variable se omiten.
- `cs.vocalshift.live` existe en `createProcessor` pero **no** en `effectTypes()`: no exponerlo sin medir CPU en el teléfono (VOCALSHIFT_LIVE_STUDY.md).
- Siguiente paso: que Diego haga las pruebas 2-4 y 8 de la guía (el arreglo principal) en el altavoz del Samsung.

## Estado anterior (0.9)

## Estado

- **Rama de trabajo**: `claude/project-thread-y494z5`, basada en la 0.8 (`claude/project-thread-nhte0l`, PR #4). PR #6 borrador contra la rama de la 0.8. **Nada fusionado** (PR #1 a #6): requiere autorización de Diego. El PR #5 (0.8.1, interfaz de VocalShift, versionCode 11) va en paralelo; la 0.9 no toca esa pantalla.
- **APK de prueba**: `/mnt/project-files/caucesound/caucesound-0.9.0-signature-sounds-preview.apk`, versionCode 12, `0.9.0-signature-sounds-preview`, misma firma Preview. **No** probada en teléfono.
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/`. **No regenerar.** `CAUCE_SIGNING=/mnt/project-files/caucesound/signing/keystore.properties CAUCE_PACKS=<carpeta con packs/> ./gradlew assemblePreview`.
- **Paquetes de sonidos**: `/mnt/project-files/caucesound/paquetes-0.9/` (4 ZIP + SHA256SUMS.txt). Se reconstruyen con `cauce_packs` (`desktop/tools/packbuilder`) a partir de VCSL y Karoryfer en los commits de SOUND_LICENSES_0.9.md. El piano (42,6 MB) no va en el APK: se importa.
- **Muestras**: `/mnt/project-files/caucesound/muestras-0.9/` (LEEME.md y metricas.json). **Capturas**: `/mnt/project-files/caucesound/capturas-0.9.0/` (renders JVM en inglés).
- **Validación física**: ninguna desde la 0.3.0 salvo lo que Diego probó de la 0.6.0. Guía nueva: **SAMSUNG_ACCEPTANCE_0.9.md** (24 pruebas).
- **Formato de proyecto**: v4 solo cuando se usa un sonido de paquete o kit de muestras; si no, v3 igual que la 0.8.
- **Submódulos** sin cambios: `solo-ai-studio@fe8445b`, `vocalshift-ai-studio@10e679c`, `signalsmith-stretch@88061fd`, `signalsmith-linear@7bb1c8f`.
- **Fuera de alcance por decisión de Diego**: FrostBitten Studio y el DAW web de Ronaldo (no investigar, no DigitalOcean, no Cauce Service). Preguntas preparadas en CAUCESOUND_INTEROPERABILIDAD_FUTURA.md.

## Qué leer

| Documento | Para qué |
|---|---|
| STATUS.md | Qué está implementado, probado, compilado y qué no |
| SAMSUNG_ACCEPTANCE_0.9.md | Las 24 pruebas de la 0.9 que solo puede hacer Diego |
| CAUCE_SAMPLER.md | Auditoría del sonido, arquitectura del sampler, memoria y rendimiento |
| CAUCE_PACK_FORMAT.md | Formato de paquete, manifiesto, importación segura |
| SOUND_LICENSES_0.9.md / SOUND_TAXONOMY.md | Procedencia y licencias; familias, estilos y nombres |
| AUTOMIX.md | Reglas del AutoMix y cómo se calibraron |
| CAUCESOUND_INTEROPERABILIDAD_FUTURA.md | Qué sirve ya para el DAW web y qué preguntar |
| SAMSUNG_ACCEPTANCE_0.8.md | Las ocho pruebas de la 0.8 que solo puede hacer Diego |
| muestras-0.8/LEEME.md (carpeta del proyecto) | Qué escuchar en los WAV de Variar y Regenerar, con mediciones |
| SAMSUNG_ACCEPTANCE_0.7.md | Las cinco pruebas de la 0.7 que solo puede hacer Diego |
| SOLO_AI_ALTERNATIVES.md | Causa y corrección de «A-D suenan iguales», con mediciones |
| FORMAT_V3.md | Pieza de batería en canal propio y migración v2 → v3 |
| ARCHITECTURE.md §9 | Mixer Pro, cadenas y Channel Rack |
| CAUCE_LINK.md | Puente con el ordenador: emparejamiento, versiones, conflictos, viabilidad de un VST3 |
| CAUCESOUND_STUDIO.md | Qué se reutiliza en Windows, audio (WASAPI/ASIO), interfaz, VST3, orden propuesto |
| ARCHITECTURE.md §7 y §8 | Trabajos offline, clips con dos versiones, exportación para DAW |
| PLUGIN_CONTRACT.md | Cómo entran los plugins, sus reglas y los comandos de la API |
| RESEARCH_0.5.md | Spatial Echo, AI Choir, congelar pista, PDC en buses |

## Pruebas (último resultado, 2026-11-13)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 145/145 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | **145/145** prueba por prueba, sin informes de ASan ni UBSan (fuzz de comandos con 1500 iteraciones) |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | **145/145** prueba por prueba, sin avisos de carrera |
| ARM64 | — | **No ejecutado** (sin QEMU ni compilador cruzado). Solo compilación con el NDK |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 87/87 |
| Lint | `./gradlew lintDebug` | 0 errores, 37 avisos |
| Paquete para DAW | `./build/cauce verify paquete.zip` y `python3 desktop/tools/verify_daw_package.py` (dentro del ZIP descomprimido; necesita `pip install mido`) | Correcto en el ejemplo |

Con sanitizadores conviene ejecutar prueba por prueba: la suite entera tarda más de una hora y
`fuzz_command_api…` sola pasa de 20 minutos (se puede acortar con `CS_FUZZ_ITERATIONS`).

Datos de prueba de la interfaz: JSON real del motor. Se regeneran con
`./build/cs_render --fixtures05 android/app/src/test/resources/fixtures` (**ojo**: en la 0.8 regenerarlos todos rompe 8 pruebas de la JVM que dependen de valores antiguos; solo se regeneró `catalog.json` con `cs_render --catalog`); los anteriores (incluido `state_rack.json` de la 0.7) con
`CS_FIXTURE_DIR=android/app/src/test/resources/fixtures ./build/cs_core_tests ui_fixtures`.

## Entorno de compilación (nube)

- SDK en `/opt/android-sdk` (platforms 36, build-tools 36.0.0, NDK 27.2.12479018, CMake 3.31.6); `android/local.properties`.
- Maven Central devolvía 429: `~/.gradle/init.d/mirror.gradle` redirige las URL de Maven Central de settings.gradle al espejo
  `https://maven-central.storage-download.googleapis.com/maven2/` (solo en esta máquina, no en el repo).
- Robolectric descarga `android-all`: en este entorno exporta `ROBOLECTRIC_REPO_URL` con ese espejo.
- Sin KVM: no hay emulador. No generar capturas «de teléfono»: las únicas son renders JVM.

## Problemas y límites conocidos

1. **Nada probado en un teléfono desde la 0.3.0** (Diego probó la 0.6.0 a mano; la 0.7 no). Es el riesgo principal.
2. Solo AI: las alternativas son medibles y distintas, pero falta el oído. La tonalidad detectada sigue siendo poco fiable (1 de 15 exacta, un error con confianza 0,68).
3. Mixer Pro: sin inserciones en el master (no hay «Master ligero») y sin efectos de modulación. En los buses se prohíben los efectos con latencia (no hay PDC en buses).
4. Channel Rack: la 0.6 no abre proyectos v3. El *soft-clip* de la batería no actúa sobre las piezas enrutadas. El Mixer no se desplaza solo hasta la tira elegida.
5. VocalShift: sin cambios en la 0.7 (falta voz humana con licencia para una comparación A/B honesta). Solo offline. Identidad Dark Rose / Neon Violet sin hacer.
6. Heredados: paquete para DAW de 60 a 180 MB; `cauce` solo en Linux; sin sampler, sin congelar pista, sin Spatial Echo ni AI Choir; sin pantalla de licencias de terceros.

## Próximas tareas sugeridas (en este orden)

1. **Probar la 0.7.0 en el Samsung** con SAMSUNG_ACCEPTANCE_0.7.md, empezando por Solo AI (prueba 1).
2. Corregir lo que salga.
3. Si Diego aprueba el Mixer: inserciones en el master (InsertChain en la salida, con «Master ligero») y desplazar el Mixer hasta la tira elegida.
4. VocalShift: conseguir 2-3 voces con licencia (o grabadas por Diego) y hacer el A/B de naturalidad antes de tocar el DSP.
5. Detección de tonalidad sin la batería, antes de prometer nada con beats reales.

## Decisiones que necesitan al propietario

- Fusionar los PR #1, #2 y #3.
- Aceptar que los proyectos guardados con la 0.7 ya no se abren en la 0.6.
- Material de voz para evaluar VocalShift.
- Licencias, Spatial Echo, AI Choir, clave de producción y publicación (sin cambios desde la 0.6).
