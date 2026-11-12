# Para retomar el trabajo (actualizado 2026-11-12, fin de la misión 0.6 «Professional Production»)

## Estado

- **Rumbo**: CAUCESOUND es un ecosistema de tres productos sobre un mismo motor y formato: **Mobile** (existe),
  **Cauce Link** (diseñado: CAUCE_LINK.md) y **Studio** para Windows (analizado: CAUCESOUND_STUDIO.md).
- **Rama de trabajo**: `claude/project-thread-hxmmji`. Pull request #2 (borrador) contra `claude/project-thread-w0xy20`,
  que a su vez es el PR #1 contra `main`. **Ninguno fusionado**: requiere autorización de Diego.
- **APK de prueba**: `/mnt/project-files/caucesound/caucesound-0.6.0-production-preview.apk`,
  `com.caucesound.app.preview`, versionCode 8, versionName `0.6.0-production-preview`. SHA-256 en STATUS.md.
  Mismo certificado que todas las Preview: se instala encima de la 0.5.0 (comprobado por certificado y versionCode;
  **no** probado en teléfono). Los APK anteriores siguen en la misma carpeta, sin tocar.
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/`. Fuera de git. **No regenerar.**
  Compilar firmado: `CAUCE_SIGNING=/mnt/project-files/caucesound/signing/keystore.properties ./gradlew assemblePreview`.
- **Capturas**: renders JVM en `/mnt/project-files/caucesound/capturas-0.6.0/` (no son de teléfono; salen en inglés
  porque el entorno de pruebas usa esa configuración regional).
- **Muestras**: `/mnt/project-files/caucesound/muestras-0.6/` (antes y después del sonido, igualados de volumen, y un
  paquete para DAW de ejemplo; leer su `LEEME.md`).
- **Validación física**: ninguna. Lo último que Diego probó en el Samsung fue la 0.3.0.
  La guía para hacerlo es **SAMSUNG_ACCEPTANCE_0.6.md** (cinco experiencias).
- **Submódulos** (4, sin cambios): `solo-ai-studio@fe8445b`, `vocalshift-ai-studio@10e679c`,
  `signalsmith-stretch@88061fd`, `signalsmith-linear@7bb1c8f`. Clonar con `git submodule update --init`.
- **Formato de proyecto**: sigue en la versión 2. Los proyectos de la 0.5 se abren sin migración; el campo `match` del
  procesado de voz de la 0.5 se ignora a propósito.

## Qué leer

| Documento | Para qué |
|---|---|
| STATUS.md | Qué está implementado, probado, compilado y qué no |
| SAMSUNG_ACCEPTANCE_0.6.md | Las cinco pruebas que solo puede hacer Diego |
| CAUCE_LINK.md | Puente con el ordenador: emparejamiento, versiones, conflictos, viabilidad de un VST3 |
| CAUCESOUND_STUDIO.md | Qué se reutiliza en Windows, audio (WASAPI/ASIO), interfaz, VST3, orden propuesto |
| ARCHITECTURE.md §7 y §8 | Trabajos offline, clips con dos versiones, exportación para DAW |
| PLUGIN_CONTRACT.md | Cómo entran los plugins, sus reglas y los comandos de la API |
| RESEARCH_0.5.md | Spatial Echo, AI Choir, congelar pista, PDC en buses |

## Pruebas (último resultado, 2026-11-12)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 128/128 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | @@ASAN@@ |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | @@TSAN@@ |
| ARM64 | — | **No ejecutado** (sin QEMU ni compilador cruzado). Solo compilación con el NDK |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 66/66 |
| Lint | `./gradlew lintDebug` | 0 errores, 38 avisos |
| Paquete para DAW | `./build/cauce verify paquete.zip` y `python3 desktop/tools/verify_daw_package.py` (dentro del ZIP descomprimido; necesita `pip install mido`) | Correcto en el ejemplo |

Con sanitizadores conviene ejecutar prueba por prueba: la suite entera tarda más de una hora y
`fuzz_command_api…` sola pasa de 20 minutos (se puede acortar con `CS_FUZZ_ITERATIONS`).

Datos de prueba de la interfaz: JSON real del motor. Se regeneran con
`./build/cs_render --fixtures05 android/app/src/test/resources/fixtures`; los anteriores con
`CS_FIXTURE_DIR=android/app/src/test/resources/fixtures ./build/cs_core_tests ui_fixtures`.

## Entorno de compilación (nube)

- SDK en `/opt/android-sdk` (platforms 36, build-tools 36.0.0, NDK 27.2.12479018, CMake 3.31.6); `android/local.properties`.
- Maven Central devolvía 429: `~/.gradle/init.d/mirror.gradle` antepone el espejo
  `https://maven-central.storage-download.googleapis.com/maven2/` (solo en esta máquina, no en el repo).
- Robolectric descarga `android-all`: en este entorno exporta `ROBOLECTRIC_REPO_URL` con ese espejo.
- Sin KVM: no hay emulador. No generar capturas «de teléfono»: las únicas son renders JVM.

## Problemas y límites conocidos

1. **Nada probado en un teléfono desde la 0.3.0.** Están sin validar el hotfix de Play (0.3.1), la 0.4, la 0.5 y la
   0.6. Es el riesgo principal del proyecto y crece con cada entrega.
2. Sonido 3.0: hay diferencia medible (menos graves, melodía presente), no una mejora demostrada. Falta el oído.
   Solo cambia en canciones nuevas. Sin compresión lateral bombo/bajo. El hip-hop sigue siendo el más grave (80 %).
3. Solo AI: la tonalidad detectada es poco fiable con mezclas completas (3 de 15 reconocidas) y hay un caso
   equivocado con confianza 0,68 (medido en `solo_ai_analysis_on_caucesound_songs…` como defecto conocido).
4. VocalShift: solo offline; naturalidad sin juzgar; el igualado A/B usa RMS, no sonoridad percibida.
5. Paquete para DAW: 60 a 180 MB por canción; no es un proyecto nativo de ningún DAW.
6. `cauce` solo compilado en Linux. En Windows fallarían rutas con acentos (ver CAUCESOUND_STUDIO.md §2).
7. Sin sampler. Sin congelar pista. Sin Spatial Echo. Sin AI Choir (paquete no localizado).
8. Buses: los efectos con latencia están prohibidos (no hay PDC en buses).
9. No hay pantalla de «Licencias de terceros» en la app (hace falta antes de publicar por los avisos MIT).
10. Heredados: el mezclador de pista no entra en deshacer; «Repetir ×2» son dos pasos; favoritos y cuenta previa no
    viajan con el proyecto; los proyectos de la app de la fase 1 no son accesibles desde la Preview.

## Próximas tareas sugeridas (en este orden)

1. **Probar la 0.6.0 en el Samsung** con SAMSUNG_ACCEPTANCE_0.6.md. Sin esto, seguir añadiendo es construir a ciegas.
2. Corregir lo que salga. Candidatos previsibles: tiempos de exportación y de VocalShift en el teléfono, tamaño del
   paquete, permisos de micrófono en «Idea rápida», equilibrio del nuevo sonido en altavoces reales.
3. CI en Windows para `cauce` y las pruebas del motor (paso 1 de CAUCESOUND_STUDIO.md §8).
4. Detección de tonalidad: probar a analizar sin la batería (separación por bandas o por transitorios) antes de
   prometer nada con beats reales.
5. Cauce Link paso 2 (campos `revision` y `parents`, detección de divergencia sin red), solo si Diego usa de verdad
   el paquete para DAW.

## Decisiones que necesitan al propietario

- Fusionar los PR #1 y #2.
- Si el sonido 3.0 se queda, se ajusta o se retira (tras la experiencia 2).
- Licencia de CAUCESOUND y de los núcleos de Solo AI, VocalShift y Spatial Echo.
- Spatial Echo: rama de referencia en `3D` y permiso para crear allí un núcleo sin JUCE.
- AI Choir Studio: subir el paquete al proyecto o indicar el repositorio.
- Si Cauce Link por Wi-Fi y Studio merecen el esfuerzo, o basta con mover archivos.
- Clave de firma de producción y publicación (Play Console: 25 USD; nada gastado).
