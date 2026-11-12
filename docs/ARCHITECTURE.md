# Arquitectura de CAUCESOUND

Fecha: 2026-11-12 · Fase 1 (prototipo Android).

Leyenda usada en todo el repositorio:
- **Implementado**: el código existe y compila.
- **Probado automáticamente**: hay una prueba reproducible que pasa (en este entorno: Linux x86-64 y ARM64 emulado con QEMU).
- **Validado físicamente**: comprobado en un teléfono real. **En esta fase no hay nada validado físicamente**, porque
  este entorno no tiene ningún dispositivo Android ni emulador (no hay KVM).

## 1. Comparación de alternativas

Criterios: rendimiento de audio en tiempo real, complejidad, licencias comerciales, portabilidad Android/iOS y
facilidad para integrar los plugins propios (cuyos núcleos ya son C++17 sin JUCE, ver [PLUGINS.md](PLUGINS.md)).

| Opción | Audio en tiempo real | UI táctil | Licencia / coste | Portabilidad | Integrar plugins propios | Valoración |
|---|---|---|---|---|---|---|
| **A. JUCE completo** (motor + UI JUCE en ambas plataformas) | Bueno (usa Oboe en Android y Core Audio en iOS) | Débil: JUCE dibuja sus propios controles; accesibilidad, teclado, texto, gestos y aspecto nativo requieren mucho trabajo extra | AGPLv3 o EULA comercial. Según la verificación registrada en `vocalshift-ai-studio/docs/TECH_EVALUATION.md` (05-11-2026, **no re-verificada hoy**): Starter gratis hasta 20 000 USD de ingresos anuales, Indie hasta 300 000 USD (40 USD/mes u 800 USD perpetuo), Pro sin límite | Muy alta (un solo código) | Directa | Viable, pero condiciona el producto a una UI poco nativa y a una licencia que crece con los ingresos |
| **B. Núcleo C++ propio + E/S nativa (Oboe / Core Audio) + UI nativa** (Compose en Android, SwiftUI o Compose Multiplatform en iOS) | Máximo control; lo que usan las apps de referencia del sector | Excelente: componentes nativos, accesibilidad, rendimiento de la UI | Oboe: Apache 2.0. Núcleo propio. Sin regalías | Núcleo 100 % compartido; UI se escribe por plataforma (o se comparte con Compose Multiplatform) | Directa: los núcleos de los plugins son C++17 puro | **Recomendada** |
| **C. JUCE solo como biblioteca DSP/formatos dentro de B** | Igual que B | Igual que B | Arrastra la licencia de JUCE aunque se use poco | Alta | Directa | Posible más adelante si se necesita algo concreto de JUCE (p. ej. hospedar AUv3 con su wrapper); hoy no aporta lo suficiente |
| **D. Flutter / React Native + motor C++ por FFI** | El audio sigue en C++; bien | Buena y compartida | BSD / MIT | Alta | Directa por FFI | Añade un tercer lenguaje y un puente más; las vistas de timeline/piano roll de alto rendimiento acaban siendo canvas a medida igualmente |
| **E. Kotlin/Swift puro (sin C++)** | Insuficiente: recolector de basura y JNI en el camino crítico; no hay cómo compartir el motor | Excelente | — | Nula para el motor | Habría que reescribir los plugins | Descartada |

### Decisión

**Opción B.** Motivos:
1. **Los plugins ya están preparados para ella.** VocalShift y Solo AI tienen núcleos C++17 sin JUCE (`vs_core`,
   `solo_core`); compilé ambos para ARM64 sin cambios (ver PLUGINS.md). JUCE solo está en sus capas de plugin VST3.
2. **Licencia**: sin dependencia de una licencia comercial que escala con los ingresos. Todo lo incorporado es
   Apache 2.0 / MIT (ver [LICENSES.md](LICENSES.md)).
3. **Calidad de UI**: un DAW móvil compite por la experiencia táctil; Compose y SwiftUI dan eso sin pelear con el framework.
4. **Riesgo contenido**: el contrato entre UI y motor es un único API de comandos JSON (`core/include/cs/Api.h`)
   más tres lecturas directas de alta frecuencia. La UI de iOS consumirá exactamente el mismo contrato.

Inconveniente aceptado: la UI se escribe dos veces (Compose y SwiftUI) salvo que adoptemos Compose Multiplatform en
iOS. **Decisión pendiente para la fase iOS** (ver [IOS.md](IOS.md)); no afecta al motor.

## 2. Estructura

```
core/                      C++17 puro, sin dependencias de plataforma (compartido Android / iOS / escritorio)
  include/cs/Engine.h      motor en tiempo real (hilo de audio)
  include/cs/Session.h     proyecto abierto: edición, deshacer, grabación, guardado, exportación (hilo de control)
  include/cs/Project.h     modelo de proyecto + JSON versionado + guardado atómico
  include/cs/Processor.h   interfaz modular de efectos e instrumentos (plugins internos)
  include/cs/Api.h         API de comandos JSON para las interfaces
  include/cs/Wav.h         lectura/escritura WAV, escritura incremental y reparación
  include/cs/Resampler.h   conversión de frecuencia (importación)
  include/cs/Midi.h        notas, patrones, clips MIDI, ticks↔frames, cuantización, swing, expansión a eventos
  include/cs/Instruments.h presets de Cauce Synth y kits de Cauce Drums (catálogo)
  src/Synth.cpp, Drums.cpp instrumentos propios;  src/Effects.cpp  Cauce FX
  src/SessionMusic.cpp     edición musical, plantillas, inserciones
  tests/                   58 pruebas + bench
android/                   app Android (Kotlin + Compose) y puente nativo (JNI + Oboe)
  app/src/main/cpp/        AudioIO (Oboe dúplex), JniBridge
third_party/nlohmann/      JSON (MIT, un solo archivo)
docs/
```

## 3. Hilos y reglas de tiempo real

```
 UI (Compose)  ──comandos JSON──►  hilo de control  ──publish(RenderGraph)──►  [atómico]  ──►  hilo de audio (Oboe)
      ▲                              Session                                                    Engine::process
      │                                 │  atómicos: volumen, pan, mute, solo, transporte ────────────►│
      └── posición / medidores ◄────────┼──────────────────────── atómicos ◄────────────────────────────┘
                                        │                                   cola SPSC (entrada de micrófono)
                                   hilo de disco (Recorder) ◄───────────────────────────────────────────┘
```

- El hilo de audio **no reserva memoria, no bloquea, no toca disco ni red**. Comprobado: la prueba
  `engine_process_does_not_allocate` sustituye `operator new` y cuenta 0 reservas en 2000 bloques con 16 pistas,
  metrónomo, grabación y monitorización.
- El estado estructural (pistas, clips, inserciones) se publica como un `RenderGraph` inmutable mediante un
  intercambio atómico. El grafo anterior nunca se libera en el hilo de audio: va a una cola de basura que vacía el
  hilo de control (`engine_graph_swap_stress_is_safe`: 3000 publicaciones con el audio en marcha, sin fugas; TSan limpio).
- Parámetros continuos por atómicos con rampa de 10 ms (sin clics; `engine_mute_solo_and_balance_pan`).
- La grabación pasa por una cola SPSC sin bloqueo hacia un hilo de disco que escribe un WAV float32 incremental y
  actualiza el encabezado cada segundo, más un archivo auxiliar `.rec.json` con la posición. Tras un cierre
  inesperado, abrir el proyecto recupera la toma (`recording_is_recovered_after_unexpected_close`).
- IA y procesamiento offline: irán en hilos de trabajo propios y entregarán resultados (audio o MIDI) como nuevas
  fuentes/clips por la misma vía que una importación. Nunca en el callback.

## 4. Decisiones de la fase 1 (reversibles)

| Decisión | Motivo | Coste / cuándo revisar |
|---|---|---|
| Frecuencia de proyecto fija 48 kHz; Oboe convierte si el hardware difiere | Es la frecuencia nativa habitual en Android; simplifica todo el motor | Si un dispositivo no es 48 kHz, Oboe añade algo de latencia. Medir en el Samsung |
| Audio de las fuentes en memoria (float) | Simple y sin E/S en el callback | ~11,5 MB por minuto mono, 23 MB estéreo. **Limita la duración total del proyecto**. Fase 2: lectura en streaming desde disco con hilo de precarga |
| Fuentes guardadas como WAV float32 dentro del proyecto | Sin pérdidas, sin licencias de códecs | Ocupa más que FLAC. Revisar si el espacio molesta |
| Ley de pan de balance (centro = 0 dB) | Predecible y sin pérdida de nivel en estéreo | Añadir ley de potencia constante como opción |
| Fundidos lineales | Exactos y comprobables | Añadir curvas (igual potencia, exponencial) |
| Mezcla (volumen, pan, mute, solo) fuera de deshacer | Evita llenar el historial al mover deslizadores | Preguntar preferencia de producto |
| Una sola pista armada; grabación mono | Alcance de la fase 1 | Multi-entrada con interfaces USB en fase 2 |
| Sin servicio en primer plano: salir de la app detiene y guarda la grabación | Evita grabaciones perdidas si el sistema mata el proceso | Fase 2: servicio `microphone` para grabar con la pantalla apagada |
| Compensación de latencia de grabación manual (ms) | No hay medición física disponible | Fase 2: calibración automática por bucle (altavoz→micrófono o cable de loopback) |

## 5. Interfaz modular de plugins

`cs::AudioProcessor` (efectos) y `cs::Instrument` (instrumentos MIDI) en `core/include/cs/Processor.h`. Las pistas
tienen una cadena de inserciones persistente en el proyecto (`inserts`: tipo + estado JSON). Hoy solo existe
`cs.gain` para validar la cadena (prueba `processor_gain_insert_in_chain`). Cómo encajan los plugins propios:
ver [PLUGINS.md](PLUGINS.md). Pendiente: compensación de latencia de inserciones (necesaria para VocalShift, 150 ms).

Desde la fase 1.2 existen además `cs.synth`, `cs.drums` (instrumentos) y los efectos `cs.filter`, `cs.eq`,
`cs.saturation`, `cs.compressor`, `cs.delay`, `cs.reverb`. `tailSamples()` está en la clase base: la exportación
suma la cola del instrumento y la de los efectos de cada pista.

## 6. MIDI, instrumentos y formato v2 (fase 1.2)

**Tiempo musical**: posiciones y duraciones MIDI en ticks (PPQ 960; un paso de batería = 240). Se convierten a
frames con el tempo del proyecto (`llround`), así que cambiar el tempo mueve las notas y no el audio
(`tempo_change_moves_midi_events_but_not_audio`).

**Planificación**: al publicar el grafo, el hilo de control expande cada clip (notas o patrón repetido, con swing) a
una lista ordenada de eventos en frames absolutos por pista. En el hilo de audio, `Engine` parte el bloque en cada
evento y llama a `handleMidi` + `process` por tramos: el resultado es idéntico con cualquier tamaño de bloque
(probado con bloques de 1 a 4096 y aleatorios). Note Off va antes que Note On en el mismo frame.

**Sin notas colgadas**: stop, seek, salto de bucle y cambios de grafo envían «all notes off» (release suave); un
grafo nuevo solo corta notas si cambió el hash de eventos de esa pista. Probado con ediciones durante la reproducción.

**Bucle**: rango [inicio, fin) en frames por atómicos; se ignora durante la grabación para no solapar tomas.

**MIDI en vivo** (pads, teclado, vista previa del piano roll): cola SPSC por pista desde JNI, sin pasar por el hilo
de control; los instrumentos suenan también con el transporte parado.

**Instrumentos**: instancias vivas por pista en la sesión (`instrumentInstances`), preparadas fuera del hilo de
audio. Cambiar el kit regenera los sonidos en el hilo de control y publica una instancia nueva. La exportación usa
copias offline con el mismo estado.

**Formato de proyecto v2**: añade `kind` (audio/synth/drums), `instrument` (tipo, estado JSON, preset),
`patterns`, `midiClips` (notas como `[tecla, velocidad, canal, inicio, duración]`), `loop`, `template`, `ppq`.
Se leen proyectos v1 sin cambios; al guardar se migra y se conserva una sola vez `project.v1.json`. Un proyecto con
versión futura o datos musicales inválidos se rechaza con error, sin tocar el archivo.

**Extensiones previstas** (el modelo las admite sin romper el formato): pitch bend, CC y aftertouch como eventos
adicionales del clip; automatización como curvas por parámetro; MIDI externo (`android.media.midi` / CoreMIDI)
entrando por la misma cola en vivo; importar/exportar archivos MIDI convirtiendo ticks (PPQ distinto se reescala).

**Interfaz Android**: las pantallas no conocen el motor: reciben `ProjectState` y la interfaz `StudioActions`
(implementada por `DawViewModel`). Por eso se pueden renderizar en la JVM con datos del motor (`UiScreensTest`).
Posición y medidores se leen en la fase de dibujo para no recomponer toda la pantalla a 60 Hz.
