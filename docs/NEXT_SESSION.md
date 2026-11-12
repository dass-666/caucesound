# Para retomar el trabajo (actualizado 2026-11-12 ~09:45 UTC, fin de la sesión nocturna)

## Estado

- **Rama**: `claude/project-thread-w0xy20` (el entorno exige este nombre; la propuesta original era
  `feat/fase-1-2-beatmaker-ui-midi`). Pull request abierto contra `main`, **sin fusionar** (requiere autorización).
- **Base**: `main` en `dbdd93b` (fase 1). Ver `git log main..claude/project-thread-w0xy20` para los commits.
- **APK de prueba**: `caucesound-0.2.0-fase1.2-preview.apk` en la carpeta del proyecto (`/mnt/project-files/caucesound/`),
  `com.caucesound.app.preview`, versionCode 2, SHA-256 `aa9224be868168a1830c1bc28b8be7effae56a7c66b5bd513652586cb207951e`
  (compilado ~09:38 UTC, incluye PDC, deshacer por gesto, .caucesound, desnormales, presets corregidos, argumentos estrictos,
  medidor de carga DSP y bus de reverb con envíos). Mismo certificado que la Preview anterior: se instala encima. Se instala junto a la app de la fase 1 (ver TESTING_ON_DEVICE.md).
- **Clave de prueba**: `/mnt/project-files/caucesound/signing/` (`caucesound-preview.jks` + `keystore.properties`).
  Fuera de git. Si se pierde, la Preview instalada tampoco podrá actualizarse (igual que pasó con la fase 1).
- **Validación física**: ninguna. Todo lo «probado» es automático (ver STATUS.md).

## Pruebas (último resultado)

| Conjunto | Comando | Resultado |
|---|---|---|
| Motor x86-64 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cs_core_tests` | 78/78 |
| ASan + UBSan | `-DCS_SANITIZE=ON` en `build-asan` | 78/78 |
| TSan | `-DCMAKE_CXX_FLAGS=-fsanitize=thread` en `build-tsan` | 78/78 |
| ARM64 (QEMU) | `-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_EXE_LINKER_FLAGS=-static`, `qemu-aarch64 ./build-arm64/cs_core_tests` | 78/78 |
| App JVM | `cd android && ./gradlew testDebugUnitTest` (añade `-Pscreenshots` para capturas) | 24/24 |
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
5. Nombres de carril largos recortados en vertical; diálogos y ajustes sin captura.
6. CI de GitHub: ahora ejecuta también las pruebas Robolectric (descargan `android-all` ~ 100-200 MB la primera vez).
   Gratis dentro de la cuota; verificar que el primer run pasa.

## Próximas tareas sugeridas (en este orden)

1. **Probar en el Samsung** con el protocolo de TESTING_ON_DEVICE.md y corregir lo que salga (prioridad absoluta:
   latencia de pads, notas colgadas, CPU).
2. Congelar pista (renderizar a audio un synth terminado). Los envíos a un bus ya están hechos.
3. Integrar VocalShift como `cs::AudioProcessor` (requiere decidir licencia y forma de dependencia).
4. Sobremuestreo x2 de sierra/cuadrada solo si se oye aliasing en notas agudas sin filtro.
5. Automatización básica (volumen y corte del filtro) y pitch bend en el piano roll.
6. Servicio en primer plano para grabar con la pantalla apagada.

## Decisiones que necesitan autorización del propietario

- Fusionar el PR en `main`.
- Licencia del código de CAUCESOUND y de VocalShift/Solo AI antes de integrarlos o distribuir.
- Clave de firma de producción y publicación (Play Console: 25 USD; nada gastado).
- «AI Choir Studio»: según la memoria del proyecto sería Spatial Echo Studio (3D, JUCE); confirmar.
- Si se acepta borrar la app de la fase 1 del teléfono una vez rescatados sus proyectos.
