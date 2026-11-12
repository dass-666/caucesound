# Para retomar el trabajo (actualizado 2026-11-12, fin de la misión «Ultimate Polish»)

## Estado

- **Rama**: `claude/project-thread-w0xy20`. Pull request #1 contra `main`, **sin fusionar** (requiere autorización de
  Diego). Al fusionar conviene «squash merge»: así los 25 WAV residuales que estuvieron en la rama (ya borrados) no
  entran en la historia de `main`.
- **APK de prueba**: `caucesound-0.3.0-polish-preview.apk` en `/mnt/project-files/caucesound/`,
  `com.caucesound.app.preview`, versionCode 3. SHA-256 en STATUS.md y en el informe de la misión. Mismo certificado que
  la 0.2.0: se instala encima sin perder proyectos (comprobado por certificado y versionCode; **no** probado en teléfono).
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/` (`caucesound-preview.jks` + `keystore.properties`).
  Fuera de git. No regenerar: si se pierde, la Preview instalada no podrá actualizarse.
- **Capturas**: renders JVM en `/mnt/project-files/caucesound/capturas-0.3.0/` (no son de teléfono).
- **Validación física**: ninguna. Todo lo «probado» es automático (ver STATUS.md).
- **Diseño**: reglas y componentes en DESIGN_SYSTEM.md.
- **AI Choir Studio**: Diego aclaró que **no** es Spatial Echo Studio (repo `3D`); son proyectos distintos. El repo de
  AI Choir Studio no está localizado: preguntar a Diego antes de integrarlo.

## Pruebas (último resultado)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 80/80 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | 80/80 |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | 80/80 |
| ARM64 (QEMU) | `-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_EXE_LINKER_FLAGS=-static`, `qemu-aarch64 ./build-arm64/cs_core_tests` | 80/80 |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 38/38 |
| Lint | `./gradlew lintDebug` | 0 errores |
| Bench | `./build/cs_bench` | proyecto trap + pad + 5 efectos: p99 1,8-4,4 % (Xeon compartido, varía; no es un teléfono) |
| Carga | `./build/cs_core_tests stress` (`CS_STRESS_PASSES`, `CS_STRESS_EDITS` para alargar) | 32 pistas: p99 17-26 % |
| Perfil | `build-prof` (RelWithDebInfo) + `valgrind --tool=callgrind ./build-prof/cs_core_tests stress_large` | synth ~30 %, reverb ~20 % |

Datos de prueba de la interfaz: JSON real del motor, se regeneran con
`CS_FIXTURE_DIR=android/app/src/test/resources/fixtures ./build/cs_core_tests ui_fixtures`.

## Entorno de compilación (nube)

- SDK en `/opt/android-sdk` (platforms 36, build-tools 36.0.0, NDK 27.2.12479018, CMake 3.31.6); `android/local.properties`.
- Maven Central devolvía 429: `~/.gradle/init.d/mirror.gradle` antepone el espejo
  `https://maven-central.storage-download.googleapis.com/maven2/` (solo en esta máquina, no en el repo).
- Robolectric descarga `android-all`: en este entorno exporta `ROBOLECTRIC_REPO_URL` con ese espejo.
- Sin KVM: no hay emulador. No generar capturas «de teléfono»: las únicas son renders JVM.

## Problemas conocidos

1. Nada probado en un teléfono: latencia de pads/teclado, gestos multitáctiles, CPU y temperatura sin medir.
2. PDC hecha y probada solo con un efecto sintético (`LatencyFx` en TestEffects.cpp); falta validarla con VocalShift
   real. Tras arrancar/seek, los primeros L frames de la pista latente son el retardo del efecto.
3. Los proyectos de la app fase 1 no son accesibles desde la Preview (apps distintas). Ya existe exportar/importar
   `.caucesound` en la Preview, pero la app fase 1 no lo tiene. El APK de la fase 1 no es depurable
   y bloquea las copias de datos: el rescate es por pistas (Solo + Exportar WAV en la fase 1, importar en la Preview).
   Ver TESTING_ON_DEVICE.md, «Rescatar proyectos de la fase 1».
4. El mezclador de pista (volumen, pan, mute, solo) no entra en deshacer (decisión de la fase 1). Las perillas de
   synth, batería y efectos sí, agrupadas por gesto.
4b. Proyectos grandes: 32 pistas usan 17-26 % de un Xeon; en un teléfono probablemente no cabe. Ya hay bus de reverb
   compartido (~45-50 % menos que una reverb por pista en 8 synths); falta congelar pista. Ver STATUS.md.
4c. Buses: la PDC no compensa latencia de efectos dentro de un bus (ningún efecto propio la tiene hoy; importará con
   VocalShift). La app solo muestra la perilla Send del primer bus (el motor admite 4).
5. Diálogos con campos de texto y Ajustes siguen sin captura dentro de su ventana (Robolectric no llega a reposo);
   el contenido del diálogo de tempo sí se prueba y se captura.
5b. «Repetir ×2» del piano roll son dos pasos de deshacer. Favoritos y cuenta previa no viajan con el proyecto.
6. CI de GitHub: ahora ejecuta también las pruebas Robolectric (descargan `android-all` ~ 100-200 MB la primera vez).
   Gratis dentro de la cuota; verificar que el primer run pasa.

## Próximas tareas sugeridas (en este orden)

1. **Probar la 0.3.0 en el Samsung** con el protocolo de TESTING_ON_DEVICE.md (pruebas 1-29) y corregir lo que salga.
   Prioridad: latencia de pads, cuenta previa alineada, tamaño táctil real, CPU.
2. Congelar pista (renderizar a audio un synth terminado) para proyectos grandes; pensar antes cómo se deshace y
   cómo se guarda (cambio de formato: habría que versionarlo).
3. Integrar VocalShift como `cs::AudioProcessor`: requiere PDC dentro de buses o impedirlo en buses, licencia y forma
   de dependencia decididas por Diego.
4. Indicador de carga DSP y XRuns a la vista en el transporte (los datos medidos ya existen en Ajustes de audio).
5. Piano roll: notas fantasma de otras pistas y duración por defecto de la nota.
6. Automatización básica (volumen y corte del filtro) y servicio en primer plano para grabar con la pantalla apagada.

## Decisiones que necesitan autorización del propietario

- Fusionar el PR en `main`.
- Licencia del código de CAUCESOUND y de VocalShift/Solo AI antes de integrarlos o distribuir.
- Clave de firma de producción y publicación (Play Console: 25 USD; nada gastado).
- «AI Choir Studio»: según la memoria del proyecto sería Spatial Echo Studio (3D, JUCE); confirmar.
- Si se acepta borrar la app de la fase 1 del teléfono una vez rescatados sus proyectos.
