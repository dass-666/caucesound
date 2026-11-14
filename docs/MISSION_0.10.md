# Misión 0.10 «Sound Engine & Music Quality»

Rama `claude/mission-010-sound-engine`, basada en la 0.9.2 (`claude/project-thread-sol38i`, PR #8). versionCode 15,
`0.10.0-sound-engine-preview`, misma firma Preview. Los PR anteriores no se han tocado ni fusionado.

**Nada se ha probado en un teléfono** (tampoco la 0.9.2). «Probado» significa pruebas automáticas en la nube, en Linux
x86-64: el motor C++ y la app en la JVM con Robolectric. **Nadie ha escuchado los sonidos ni las plantillas nuevas**:
lo que hay son mediciones, y una medición que cambia no demuestra que algo suene mejor.

## 1. Estabilidad: la prueba inestable del sampler

- **Reproducción**: con un solo núcleo (`taskset -c 0`) fallaba 30 de 30 veces, con 3 instancias de instrumento vivas
  cuando la prueba esperaba 0.
- **Causa**: no era una carrera de datos (TSan no avisaba). La prueba paraba el hilo de audio antes de que este
  adoptara el grafo vacío que publica `close()`. El grafo anterior (batería, teclas e instrumento de escucha) seguía
  activo y sus instrumentos, vivos. Era una expectativa de la prueba mal sincronizada **y además un efecto real**: la
  memoria de un proyecto cerrado no se liberaba hasta la siguiente publicación.
- **Corrección**: `Engine::settle()` espera a que el audio adopte el grafo publicado (o se rinde si el audio no late) y
  libera lo anterior; `Session::close()` la usa. La prueba no se deshabilitó: ahora espera de forma determinista.
- **Resultado**: 0 fallos de 40 en un núcleo, 0 de 20 con seis procesos de carga, y 0 de 5 con ASan y con TSan, sin
  informes. Pruebas nuevas: cerrar un proyecto libera sus instrumentos (sin hilos, determinista) y 60 ciclos de
  abrir, reproducir y cerrar proyectos con sampler y plugins sin fugas.

### «Variar» una parte no siempre llegaba al cambio mínimo (defecto de la 0.9.2, encontrado por la CI)

- **Síntoma**: la primera CI de esta rama falló en `EndToEndVariationsTest`: un botón de Variar cambió su parte menos
  del 15 %. En local había pasado.
- **Causa**: la interfaz elige la semilla al azar y las pruebas de la 0.9.2 usaban semillas fijas. Medido con 400
  pulsaciones por estilo sobre el commit de la 0.9.2: en Reggaetón 38 de 400 quedaban por debajo del 15 % (casi siempre
  la batería, con cambios del 11 al 15 %), en Hip-hop 10 de 320 y de 0 a 3 en el resto. Ocho intentos de una sola
  pasada no bastaban; con 64 intentos tampoco (28 de 400). El «cambio garantizado» de la 0.9.2 no estaba garantizado.
  Nunca dejaba la música igual: cambiaba, pero poco.
- **Corrección**: si ocho intentos no llegan, «Variar» aplica dos pasadas de variación (intentos 9 a 16) y luego tres
  (17 a 24). El plan guarda `variantDepth` solo en ese caso; las semillas que ya funcionaban dan el mismo resultado.
  Las pruebas miden ahora también los redobles, igual que el motor.
- **Resultado**: `variations_reach_the_minimum_change_with_many_seeds`: 864 pulsaciones en los ocho estilos, todas con
  cambio de al menos el 15 %. La prueba de extremo a extremo, repetida 3 veces con semillas al azar, pasó las 3.
  **Sin escuchar**: dos o tres pasadas se alejan más de la idea original; falta el juicio de oído.

## 2. Paquetes reproducibles

- `packs/factory-packs.lock.json`: los seis paquetes que debe llevar el APK, con el SHA-256 del manifiesto y del ZIP,
  número de archivos, bytes, licencia e instrumentos, y un presupuesto de tamaño (40 MiB).
- Gradle (`verifyFactoryPacks`) comprueba antes de compilar preview o release cada archivo contra su hash, que no
  falte ni sobre ninguno, la licencia y la procedencia, y el presupuesto. Si algo falla, la compilación se detiene con
  un mensaje que dice qué paquete y qué archivo. Después comprueba lo mismo **dentro del APK**.
- La app avisa en el explorador de sonidos si falta un paquete que la compilación prometía.
- `tools/factory_packs.py` prepara la carpeta desde los ZIP, regenera el bloqueo y crea ZIP reproducibles.
  `tools/fetch_vcsl.sh` descarga las 43 tomas CC0 necesarias desde un commit fijo.
- Los WAV van ahora comprimidos sin pérdida dentro del APK.
- Formato: `docs/schemas/cauce-pack.schema.json` (JSON Schema) y la sección «Uso fuera de Android» de
  `CAUCE_PACK_FORMAT.md`: un paquete es un ZIP con manifiesto, SFZ y WAV, legible desde cualquier programa, incluido
  un DAW web. No se integró ningún proyecto externo.
- Los proyectos anteriores no cambian: los tres paquetes de la 0.9 siguen con los mismos hashes.

## 3. Sonidos y plantillas

Ver `SIGNATURE_SOUNDS_2.md`: cinco kits de batería, tres bajos y tres teclados nuevos, y el modo opcional
«Signature 2» de las plantillas. WAV comparativos con sonoridad igualada en `muestras-0.10/`.

## 4. VocalShift durante la reproducción

Ver `VOCALSHIFT_LIVE_STUDY.md`, «Avance de la 0.10». Sigue oculto: no está en el catálogo ni tiene botón. Se corrigió
el silencio de 150 ms al pulsar Play, la cola del sitio anterior al saltar y la pérdida del principio al exportar; se
añadió protección de sobrecarga, un límite de instancias y un diagnóstico de CPU en Ajustes para medirlo en el Samsung.

## 5. AutoMix 2

Ver `AUTOMIX.md`, «AutoMix 2 (0.10)»: bombo y caja por estilo, sitio para la voz, aviso de graves, prevención de
recortes y comparación A/B. Reglas deterministas, sin IA.

## 6. Pruebas ejecutadas

Todo en Linux x86-64, en la nube, el 2026-11-14. Ninguna en un teléfono.

| Conjunto | Resultado |
|---|---|
| Motor C++ Release, suite completa con los seis paquetes reales (`CS_FACTORY_PACKS`) | **188/188** (eran 169 en la 0.9.2). Incluye persistencia, sampler, exportación WAV y para DAW, render fuera de línea y la huella congelada de las ocho plantillas de siempre |
| ASan + UBSan | Pruebas nuevas y afectadas (Signature 2, AutoMix, humanizar, PDC, sampler, VocalShift, cambio de proyecto, paquetes de fábrica): **58/58**, sin informes. Tras el arreglo de «Variar», las 6 de variaciones: 6/6 |
| TSan | Las mismas **58/58** y, tras el arreglo de «Variar», las 6 de variaciones, sin avisos de carrera |
| Android JVM y Robolectric (`testDebugUnitTest` con el JNI real, `CS_HOST_JNI_DIR`) | **130/130**, 0 omitidas (eran 123) |
| Lint | 0 errores, 38 avisos (igual que en la 0.9.2) |
| Comparación de sonido | `cs_render --compare10`: 24 WAV (8 estilos × 3 versiones), 0 recortes; igualados en LUFS con `tools/loudness_match.py`. Es una medición, no una escucha |
| APK `caucesound-0.10.0-sound-engine-preview.apk` | versionCode 15, `com.caucesound.app.preview`, certificado `56b4de46…61ab2e` (el mismo, se instala encima), 485 archivos de paquetes dentro, 58,0 MB, SHA-256 `b95c6c350b2f6d654a05bd3d6be2f699fb93bd10113494ae84846cb368778ab9` |
| CI | Ver el PR. La CI no tiene los paquetes (no están en git): allí las partes que los necesitan se omiten con aviso |

**Consumo**: el APK pasa de 37,3 a 58,0 MB (+20,7 MB) por los tres paquetes nuevos. Las plantillas de siempre no
cargan nada nuevo. El uso de memoria y de CPU de Signature 2 en el teléfono **no está medido**.

## 7. Qué sigue siendo experimental

- **Signature 2**: opcional y apagado por defecto. Falta el juicio de oído.
- **VocalShift en vivo**: oculto. Falta la medición en ARM y resolver el coste del arranque.
- **AutoMix, sitio para la voz**: el umbral no está calibrado con voces reales.
- **Kits 2.0**: el equilibrio interno de cada kit (charles, palmas, percusiones) no se ha escuchado.

## 8. Cómo reproducirlo

```
tools/fetch_vcsl.sh <carpeta VCSL>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/cauce_packs <carpeta VCSL> <salida> two               # los tres paquetes 2.0
python3 tools/factory_packs.py prepare <carpeta con los 6 ZIP> <paquetes>
CS_FACTORY_PACKS=<paquetes> ./build/cs_core_tests              # motor, con los paquetes reales
./build/cs_render --compare10 <salida> <paquetes>/packs         # WAV comparativos y niveles por papel
python3 tools/loudness_match.py <salida igualada> <estilo>=<a.wav>,<b.wav>,<c.wav> ...
cd android && CAUCE_PACKS=<paquetes> ./gradlew assemblePreview  # falla con un mensaje claro si falta algo
```
