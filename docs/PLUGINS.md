# Plugins propios y formatos de plugins en móvil

Investigación de solo lectura (2026-11-12). No se modificó ningún repositorio de plugins: las compilaciones de
prueba se hicieron en una copia aparte en el entorno de trabajo.

## 1. Lo que hay en cada repositorio

| | VocalShift AI Studio | Solo AI Studio | Spatial Echo Studio (repo `dass-666/3D`, indicado como «AI Choir Studio») |
|---|---|---|---|
| Rama más reciente revisada | `feat/hybrid-engine` (06-11-2026). `main` solo tiene README | `claude/solo-ai-studio-v4-beat-melody` (06-11-2026) | `claude/project-thread-t8muvg` (06-11-2026) |
| Qué hace | Transformación vocal: pitch ±24 st, formantes, «Carácter», «Magia» (compresor + aire), de-esser, segunda voz, motor espacial | Compositor de solos/melodías (MIDI editable, 8 estilos), intérprete, guitarra modelada y sampler, efectos de guitarra, hum-to-solo, análisis de beat | Repeticiones/ecos espaciales de un fragmento con pitch y formantes por repetición, stutter, volume shaper, efectos por repetición |
| Núcleo DSP sin JUCE | **Sí**: `src/core` (`vs_core`, C++17) | **Sí**: `core/` (`solo_core`, C++17) | **No**: `src/audio` y `src/model` usan `juce::` (buffers, hilos, JSON) |
| Dependencias del núcleo | Signalsmith Stretch 1.3.2 + Linear 0.6.6 (MIT) | Ninguna | JUCE 8.0.15, Signalsmith Stretch |
| Tiempo real | Probado por ellos: 0 reservas en `process()`, sin locks | Probado por ellos: 0 reservas; FTZ/DAZ con soporte explícito de ARM64 (`FPCR`) | Hilos JUCE para el render de pitch |
| Interfaz de proceso | `vs::VocalChain::process(float* const* io, int ch, int n)`, latencia fija (150 ms con Signalsmith) | `solo::SoloEngine::process(float* L, float* R, int n, const TransportState&)` | `ses::EchoEngine` (JUCE) |
| Corala / armonías | Reservado como `HarmonyEngine` en su ARCHITECTURE.md, **sin código** | — | Repeticiones con pitch (no armonía diatónica) |

Observación: el repositorio `3D` se llama a sí mismo «Spatial Echo Studio» y no contiene un generador de coros ni
armonías. Si «AI Choir Studio» es otro proyecto todavía no publicado, hay que localizarlo.

## 2. ¿Compilan y funcionan en ARM? (comprobado aquí)

Compilé los núcleos tal cual con `aarch64-linux-gnu-g++ -std=c++17 -O2` (Linux ARM64, no Android) y ejecuté sus
propias pruebas bajo QEMU. Esto comprueba **portabilidad y corrección numérica en ARM64**, no rendimiento (QEMU
emula y es mucho más lento que un teléfono).

| Núcleo | x86-64 nativo | ARM64 (QEMU) |
|---|---|---|
| `vs_core` + `CoreTests.cpp` (59 pruebas) | 59/59 | 57/59. Las 2 que fallan son **umbrales de CPU** (`cpu / seconds < 0.25`), esperable bajo emulación. Todas las pruebas de calidad, latencia, asignaciones y estado pasan |
| `solo_core` + sus 10 archivos de prueba (144 pruebas) | 144/144 (2,5 millones de comprobaciones) | 143/144. Falla `tone_oversampling_reduces_aliasing`: en ARM el sobremuestreo 2x ya mide −45,4 dB de energía no armónica (en x86 −27,7 dB), así que la condición «4x mejora 3 dB sobre 2x» no se cumple. No es un cierre ni un error de memoria, pero indica que **el resultado numérico difiere entre x86 y ARM** (hipótesis sin verificar: contracción FMA de GCC en aarch64). Hay que investigarlo antes de integrar el amplificador de guitarra |

Pendiente de medir en el Samsung: el coste real de CPU de VocalShift (en x86 sus pruebas indican ~1,5–2,5 % de un
núcleo para el pitch solo y ~27–33 % para la segunda voz completa según su propia medición). En un teléfono de gama
media eso puede ser varias veces más. **No se puede afirmar que funcione en tiempo real en el teléfono hasta medirlo.**

## 3. Cómo se integrarán en el DAW

```
cs::AudioProcessor  ◄── VocalShiftProcessor  (envuelve vs::VocalChain; efecto de inserción en pista de voz)
cs::Instrument      ◄── SoloInstrument       (envuelve solo::SoloEngine; recibe tempo/posición del transporte)
cs::AudioProcessor  ◄── SpatialEchoProcessor (requiere portar ses:: de JUCE a C++ puro, o mantenerlo offline)
Trabajo offline     ◄── generación de solos/melodías/armonías → produce MIDI editable o clips de audio nuevos
```

Trabajo necesario (estimación, no medida):
1. **Compensación de latencia de inserciones (PDC)** en el motor: VocalShift declara 150 ms. **Hecha en la fase 1.2**
   (ver §5); falta validarla con VocalShift real.
2. **Pistas MIDI y piano roll** para que Solo AI entregue MIDI editable (requisito de producto: resultados editables).
3. Contexto de transporte para instrumentos (`TransportState`: tempo, posición, reproducción).
4. Spatial Echo: es el que más trabajo pide (sustituir `juce::AudioBuffer`, `juce::Thread`, `juce::var`/JSON).
   Alternativa: mantenerlo en JUCE solo si se adopta la opción C de ARCHITECTURE.md (no recomendado por licencia).
5. Política de dependencias: integrar los núcleos como submódulo git o copia versionada (decisión pendiente; ambos
   repositorios son públicos y **sin licencia declarada**: hay que decidir la licencia antes de distribuir).

## 4. Formatos de plugins en móvil

**Hechos**:
- Un VST3 compilado para Windows **no** se puede cargar en Android ni en iOS: es código x86-64/ARM64 de Windows
  enlazado contra APIs de Windows. Ni siquiera recompilado existe un estándar de host VST3 en móvil.
- **iOS / iPadOS**: el estándar es **Audio Unit v3 (AUv3)**, extensiones de app que los hosts cargan mediante
  `AVAudioUnit` / `AUAudioUnit`. Es lo que usan Cubasis, AUM o GarageBand. CAUCESOUND en iOS podrá **hospedar AUv3
  de terceros** y, por separado, **publicar nuestros plugins como AUv3** para otros hosts.

**Inferencias razonables (verificar antes de comprometerse)**:
- **Android no tiene un formato de plugins del sistema** equivalente a AUv3. Las opciones reales son:
  1. **Plugins internos** compilados dentro de la app (lo que hace esta arquitectura). Es lo que hacen en la práctica
     los DAW móviles en Android. Recomendado.
  2. **AAP (Audio Plugins For Android)**, proyecto de código abierto que comunica host y plugin entre apps por Binder +
     memoria compartida. Adopción muy pequeña; añade latencia y complejidad. Evaluar solo si hay demanda.
  3. **MIDI entre apps** (`android.media.midi`, MIDI 2.0 desde Android 13) para controlar sintetizadores externos.
     No transporta audio.
- Cargar código nativo de otra app dentro de nuestro proceso no es una vía práctica en Android (aislamiento por app).

Recomendación: plugins propios como módulos internos en ambas plataformas; en iOS, además, host AUv3 y exportar
nuestros motores como AUv3 en una fase posterior.

## 5. Preparación para la fase 2 (estado tras la fase 1.2)

No se ha modificado ni copiado código de VocalShift, Solo AI ni Spatial Echo. Lo que el DAW ya ofrece y lo que falta:

| Necesidad | Estado | Dónde |
|---|---|---|
| Pistas MIDI editables (Solo AI entrega MIDI) | **Hecho**: clips con notas, piano roll, persistencia, cuantización | `Midi.h`, `SessionMusic.cpp`, `PianoRoll.kt` |
| Interfaz de instrumento con MIDI exacto a la muestra | **Hecho**: `cs::Instrument::handleMidi` + `allNotesOff` + `tailSamples` | `Processor.h` |
| Contexto de transporte (tempo, compás, posición en negras, reproducción) | **Definido** (`cs::TransportInfo`); falta pasarlo a cada instrumento por bloque cuando un plugin lo necesite | `Processor.h` |
| Efectos de inserción con estado, bypass, orden y deshacer | **Hecho** (Cauce FX los usa) | `Session.h` (`addInsert`…) |
| Colas en la exportación | **Hecho** (`tailSamples` en la clase base) | `Session::exportMix` |
| **Compensación de latencia (PDC)** | **Hecho** (lectura adelantada por pista); probado con un efecto sintético, no con VocalShift | `Engine.cpp` (`TrackRT::latency`) |
| Generación offline (solos, armonías) sin bloquear | Falta un hilo de trabajo con progreso y cancelación; el resultado entraría como clip MIDI (`setClipNotes`) o audio (como una importación) | — |

### PDC: diseño implementado

1. Cada pista declara su latencia = suma de `latencySamples()` de sus inserciones activas (las de bypass no cuentan).
2. La latencia máxima `Lmax` del proyecto se calcula al publicar el grafo.
3. En vez de retrasar las demás pistas con líneas de retardo (más memoria y más latencia total), la pista con
   latencia L **lee su contenido adelantado**: se reproduce desde `posición + L` (clips y eventos MIDI planificados
   L frames antes), de modo que su salida ya procesada coincide con las demás. Es exacto, no cuesta memoria y el
   motor ya trabaja en frames absolutos. Solo vale para contenido grabado o programado, no para la entrada en vivo.
4. Al arrancar, hacer seek o saltar en el bucle, la pista latente necesita precargar L frames: se renderiza ese
   tramo «en silencio» o se acepta un fundido de entrada corto (decisión de producto).
5. Monitorización en vivo con VocalShift: la latencia de 150 ms no se puede compensar en tiempo real; hay que
   avisar en la interfaz y ofrecer monitorizar sin el efecto.
6. Pruebas: impulso en una pista con un efecto de prueba de latencia conocida → debe coincidir a la muestra con un
   impulso en otra pista, en vivo y en la exportación; con bucle y con seek.

Estado: implementado. Las inserciones en bypass no se publican, así que no suman latencia. En el bucle, cada pista
latente da la vuelta en su propia posición de lectura y corta sus notas en ese punto. En el punto 4 se eligió la
opción simple: no hay precarga, los primeros L frames tras arrancar o hacer seek son el retardo propio del efecto.
Pruebas: `pdc_latent_track_stays_aligned_live_with_any_block_and_in_loop` (7200 muestras, bloques de 64/192/1000,
4 vueltas de bucle) y `pdc_export_matches_live_with_latent_insert`.

### Choir

Sigue sin identificarse un repositorio «AI Choir Studio». El repositorio `3D` es Spatial Echo Studio. Hace falta que
el propietario indique dónde está (o confirme que aún no existe) antes de planificarlo.
