# Para retomar el trabajo (actualizado 2026-11-12, fin de la misión 0.5 «Integración»)

## Estado

- **Rama de trabajo**: `claude/project-thread-hxmmji`. Pull request #2 (borrador) contra `claude/project-thread-w0xy20`,
  que a su vez es el PR #1 contra `main`. **Ninguno fusionado**: requiere autorización de Diego.
- **APK de prueba**: `/mnt/project-files/caucesound/caucesound-0.5.0-integration-preview.apk`,
  `com.caucesound.app.preview`, versionCode 7, versionName `0.5.0-integration-preview`. SHA-256 en STATUS.md.
  Mismo certificado que todas las Preview: se instala encima de la 0.4.1 (comprobado por certificado y versionCode;
  **no** probado en teléfono).
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/`. Fuera de git. **No regenerar.**
  Compilar firmado: `CAUCE_SIGNING=/mnt/project-files/caucesound/signing/keystore.properties ./gradlew assemblePreview`.
- **Capturas**: renders JVM en `/mnt/project-files/caucesound/capturas-0.5.0/` (no son de teléfono; salen en inglés
  porque el entorno de pruebas usa esa configuración regional).
- **Muestras**: `/mnt/project-files/caucesound/muestras-0.5/` (leer su `LEEME.md`).
- **Validación física**: ninguna. Lo último que Diego probó en el Samsung fue la 0.3.0.
- **Submódulos** (4): `solo-ai-studio@fe8445b`, `vocalshift-ai-studio@10e679c`, `signalsmith-stretch@88061fd`,
  `signalsmith-linear@7bb1c8f`. Clonar con `git submodule update --init`.

## Qué leer

| Documento | Para qué |
|---|---|
| STATUS.md | Qué está implementado, probado, compilado y qué no |
| PLUGIN_CONTRACT.md | Cómo entran los plugins y sus reglas |
| RESEARCH_0.5.md | Spatial Echo, AI Choir, congelar pista, PDC en buses, naturalidad de VocalShift, rendimiento |
| TESTING_ON_DEVICE.md | Pruebas físicas 30-37 (Play), 40-48 (0.4) y 49-60 (0.5) |
| ARCHITECTURE.md §7 | Trabajos offline y clips con dos versiones |

## Pruebas (último resultado, 2026-11-12)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 114/114 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | 112/112 completo en `cb2463d` + 33/33 de las pruebas afectadas por el último cambio |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | 112/112 completo en `cb2463d` + repetición de las pruebas afectadas por el último cambio (ver STATUS.md) |
| ARM64 | — | **No ejecutado** en esta sesión (sin QEMU ni compilador cruzado). Solo compilación con el NDK |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 59/59 |
| Lint | `./gradlew lintDebug` | 0 errores, 37 avisos |
| Bench | `./build/cs_bench` | trap + pad + 5 efectos: p99 6,5 % de 4 ms (x86 compartido; no es un teléfono) |

Con sanitizadores conviene ejecutar prueba por prueba: la suite entera tarda más de una hora y
`fuzz_command_api…` sola pasa de 20 minutos (se puede acortar con `CS_FUZZ_ITERATIONS`).

Datos de prueba de la interfaz: JSON real del motor. Los de la 0.5 se regeneran con
`./build/cs_render --fixtures05 android/app/src/test/resources/fixtures`; los anteriores con
`CS_FIXTURE_DIR=android/app/src/test/resources/fixtures ./build/cs_core_tests ui_fixtures`.

## Entorno de compilación (nube)

- SDK en `/opt/android-sdk` (platforms 36, build-tools 36.0.0, NDK 27.2.12479018, CMake 3.31.6); `android/local.properties`.
- Maven Central devolvía 429: `~/.gradle/init.d/mirror.gradle` antepone el espejo
  `https://maven-central.storage-download.googleapis.com/maven2/` (solo en esta máquina, no en el repo).
- Robolectric descarga `android-all`: en este entorno exporta `ROBOLECTRIC_REPO_URL` con ese espejo.
- Sin KVM: no hay emulador. No generar capturas «de teléfono»: las únicas son renders JVM.

## Problemas y límites conocidos

1. **Nada probado en un teléfono desde la 0.3.0.** Están sin validar el hotfix de Play (0.3.1), toda la 0.4 y toda la 0.5.
   Es el riesgo principal del proyecto: se acumulan tres entregas sin escuchar ni tocar.
2. VocalShift: solo offline; sin escucha previa; la naturalidad no se ha podido juzgar (no hay voz humana aquí).
   El A/B igualado usa RMS, no sonoridad percibida.
3. Solo AI: el análisis de beat solo se validó con un beat sintético. Guitarra modelada desactivada.
4. Sonido: los tres estilos suenan igual que en la 0.4 (RESEARCH_0.5.md §6). El hip-hop sigue siendo el más oscuro.
5. Buses: los efectos con latencia están prohibidos (no hay PDC en buses).
6. Sin congelar pista. Sin Spatial Echo. Sin AI Choir (paquete no localizado).
7. No hay pantalla de «Licencias de terceros» en la app (hace falta antes de publicar por los avisos MIT).
8. CI: hoy clona los submódulos sin credenciales (el trabajo `android` pasa). Si algún repositorio de plugin pasa a
   privado, la CI fallará al clonar y habrá que darle un token de solo lectura.
9. Heredados: el mezclador de pista no entra en deshacer; «Repetir ×2» son dos pasos; favoritos y cuenta previa no
   viajan con el proyecto; los proyectos de la app de la fase 1 no son accesibles desde la Preview.

## Próximas tareas sugeridas (en este orden)

1. **Probar la 0.5.0 en el Samsung** (pruebas 30-37 y 49-60). Sin esto, seguir añadiendo funciones es construir a ciegas.
2. Corregir lo que salga de esa prueba. Candidatos previsibles: tiempos de VocalShift en el teléfono, aciertos del
   análisis con beats reales, tamaño táctil de las pantallas nuevas.
3. Decidir con datos: si VocalShift convence, escucha previa de unos segundos; si hay cortes de CPU, congelar pista.
   Spatial Echo y AI Choir esperan a las decisiones de Diego (rama de referencia, permiso, paquete).

## Decisiones que necesitan al propietario

- Fusionar los PR #1 y #2.
- Licencia de CAUCESOUND y de los núcleos de Solo AI, VocalShift y Spatial Echo.
- Spatial Echo: rama de referencia en `3D` y permiso para crear allí un núcleo sin JUCE.
- AI Choir Studio: subir `AI_Choir_C7_PAQUETE_COMPLETO.zip` al proyecto o indicar el repositorio.
- Clave de firma de producción y publicación (Play Console: 25 USD; nada gastado).
