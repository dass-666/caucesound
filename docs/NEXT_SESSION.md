# Para retomar el trabajo (actualizado 2026-11-12, fin de la sesión nocturna)

## Estado

- **Rama**: `claude/project-thread-w0xy20` (el entorno exige este nombre; la propuesta original era
  `feat/fase-1-2-beatmaker-ui-midi`). Pull request abierto contra `main`, **sin fusionar** (requiere autorización).
- **Base**: `main` en `dbdd93b` (fase 1). Ver `git log main..claude/project-thread-w0xy20` para los commits.
- **APK de prueba**: `caucesound-0.2.0-fase1.2-preview.apk` en la carpeta del proyecto (`/mnt/project-files/caucesound/`),
  `com.caucesound.app.preview`, versionCode 2. Se instala junto a la app de la fase 1 (ver TESTING_ON_DEVICE.md).
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/` (`caucesound-preview.jks` + `keystore.properties`).
  Fuera de git. Si se pierde, la Preview instalada tampoco podrá actualizarse (igual que pasó con la fase 1).
- **Validación física**: ninguna. Todo lo «probado» es automático (ver STATUS.md).

## Pruebas (último resultado)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 60/60 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | 60/60 |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | 60/60 |
| ARM64 (QEMU) | `-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_EXE_LINKER_FLAGS=-static`, `qemu-aarch64 ./build-arm64/cs_core_tests` | 60/60 |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 21/21 |
| Lint | `./gradlew lintDebug` | 0 errores |
| Bench | `./build/cs_bench` | proyecto trap + pad + 5 efectos: p99 4,4 % (Xeon; no es un teléfono) |

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
3. Los proyectos de la app fase 1 no son accesibles desde la Preview (apps distintas). Falta una vía de
   exportar/importar proyectos completos (por ejemplo, ZIP del proyecto con el selector de archivos del sistema).
4. Giros de perillas (synth, FX, mezclador) no entran en deshacer.
5. Nombres de carril largos recortados en vertical; diálogos y ajustes sin captura.
6. CI de GitHub: ahora ejecuta también las pruebas Robolectric (descargan `android-all` ~ 100-200 MB la primera vez).
   Gratis dentro de la cuota; verificar que el primer run pasa.

## Próximas tareas sugeridas (en este orden)

1. **Probar en el Samsung** con el protocolo de TESTING_ON_DEVICE.md y corregir lo que salga (prioridad absoluta:
   latencia de pads, notas colgadas, CPU).
2. Exportar/importar proyecto completo (ZIP) para rescatar proyectos y compartirlos.
3. Integrar VocalShift como `cs::AudioProcessor` (requiere decidir licencia y forma de dependencia).
4. Deshacer agrupado para perillas (una entrada por gesto).
5. Automatización básica (volumen y corte del filtro) y pitch bend en el piano roll.
6. Servicio en primer plano para grabar con la pantalla apagada.

## Decisiones que necesitan autorización del propietario

- Fusionar el PR en `main`.
- Licencia del código de CAUCESOUND y de VocalShift/Solo AI antes de integrarlos o distribuir.
- Clave de firma de producción y publicación (Play Console: 25 USD; nada gastado).
- Dónde está «AI Choir Studio» (no identificado).
- Si se acepta borrar la app de la fase 1 del teléfono una vez rescatados sus proyectos.
