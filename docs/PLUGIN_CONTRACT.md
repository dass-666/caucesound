# Contrato de plugins de CAUCESOUND (actualizado en la misión 0.6)

Estado: **Solo AI y VocalShift integrados** como plugins *offline* (procesan fuera del hilo de audio y dejan un
resultado editable). Spatial Echo y AI Choir Studio **no** están integrados (ver `RESEARCH_0.5.md`). No se ha copiado
ni modificado código de los repositorios de plugins: entran como submódulos git fijados a un commit.

Vocabulario: *offline* significa que el plugin trabaja sobre un audio ya grabado y tarda lo que tarde, sin sonar en
directo; *inserción* es un efecto que procesa el sonido mientras se reproduce; *PDC* (compensación de latencia) es el
ajuste que hace el motor para que una pista con un efecto lento siga sonando a tiempo con las demás.

## 1. Las tres piezas del contrato (hecho, con pruebas)

| Pieza | Para qué | Dónde | Pruebas |
|---|---|---|---|
| `cs::AudioProcessor` / `cs::Instrument` | Efectos e instrumentos **en tiempo real**: `typeId`, parámetros con rango, estado guardable, bypass, `latencySamples()`, `tailSamples()`, MIDI exacto a la muestra | `core/include/cs/Processor.h` | Los 7 Cauce FX, el sinte y la batería (TestEffects, TestMusic) |
| Descriptor de plugin | Lo que la interfaz necesita saber sin conocer el plugin: `id`, `name`, `version` (repositorio@commit), `category` (`effect`, `generator`), `kind` (`offline`), `realtime`, latencia, `output` (`midi`, `audio`), `appliesTo`, `cancellable`, `maxSeconds` | `pluginDescriptors()` en `core/src/Api.cpp`; llega en `catalog.plugins` | `catalog_lists_only_compiled_plugins` (TestSolo) |
| `cs::JobRunner` | Trabajo pesado **fuera del hilo de audio**: cola, progreso real, cancelar, estados finales (`done`, `cancelled`, `failed`), cancelación al cambiar de proyecto | `core/include/cs/Jobs.h`, `core/src/Jobs.cpp` | `jobs_*` (3 pruebas, TestVocal) |

Reglas, todas comprobadas por pruebas:

1. Un plugin `realtime: false` **nunca** aparece como inserción en vivo. La interfaz lo presenta como «Procesar».
2. El resultado es **no destructivo**: el audio original no se toca nunca. El procesado es un archivo nuevo y el clip
   recuerda los dos (A = original, B = procesado). Deshacer lo quita.
3. Un resultado que termina cuando ya hay otro proyecto abierto **se descarta** (`vocalshift_result_never_lands_in_another_project`).
4. El archivo temporal (`audio/<id>.wav.vs.tmp`) solo se renombra a definitivo cuando el resultado se ha validado y
   aplicado. Si se cancela o falla, se borra. Al abrir un proyecto se limpian los temporales huérfanos
   (`vocalshift_stale_temp_files_are_removed_on_open…`).
5. Cerrar un proyecto cancela sus trabajos y espera a que paren (medido: 14 ms con un procesado en marcha).
6. Si un proyecto usa un plugin que esta compilación no tiene, el proyecto **se abre igual**, conserva sus datos y
   anuncia `missingPlugins` (`missing_plugin_is_reported_and_project_data_is_preserved`).
7. Si una integración no está lista, no hay botón ni pantalla para ella: la interfaz solo muestra lo que anuncia el catálogo.
8. Ningún plugin hace llamadas de red. No hay modelos neuronales ni descargas.

Límites de la cola: un trabajo a la vez (un hilo de trabajo), hasta 4 en espera, se recuerdan los 16 últimos
terminados. Un clip de más de 600 s se rechaza con un error claro en lugar de ocupar memoria sin control.

## 2. Latencia en buses: prohibida de momento

La PDC del motor compensa la latencia en **pistas**. En **buses** (envíos compartidos, por ejemplo la reverb) no está
resuelta. En lugar de simularla, `Session::addInsert` **rechaza** un efecto con latencia en un bus, con un mensaje
claro. Hoy ningún efecto propio tiene latencia (`no_internal_effect_adds_latency_so_buses_stay_aligned` lo vigila) y
VocalShift no es una inserción, así que la restricción no quita nada al usuario. Resolverlo de verdad exige retrasar
todas las pistas secas la latencia máxima de los buses; está descrito en `RESEARCH_0.5.md`.

## 3. Estado por plugin

| Plugin | Versión fijada | Qué hace en la app | Qué no hace |
|---|---|---|---|
| **Solo AI** | `solo-ai-studio@fe8445b` | Analiza un beat importado (BPM, tonalidad, acordes, con confianza) con su `BeatAnalyzer` real; propone 4 alternativas A-D con su `MelodyExplorer`; el resultado es una pista MIDI editable. Semilla reproducible | Sonido de guitarra modelada (desactivado: una prueba numérica suya difiere en ARM). No cambia el tempo ni la afinación del proyecto por su cuenta |
| **VocalShift** | `vocalshift-ai-studio@10e679c` (rama `feat/urban-spatial`) + Signalsmith Stretch `88061fd` y Linear `7bb1c8f` (MIT) | Procesa un clip de voz offline: tono, carácter, magia, segunda voz con nivel propio, espacio, presets (incluido «Urbano Espacial — Segunda voz»: +7 st, 65 %, 70 %, −12 dB). A/B sincronizado y a volumen igualado (por RMS; «Aceptar» devuelve el volumen real). Latencia real 7200 muestras (150,0 ms) compensada a la muestra | No funciona en vivo (150 ms al monitorizar no es utilizable). No usa `feat/natural-voice` ni `feat/hybrid-engine`. `NeuralVoiceConverter.h` es una interfaz vacía: no hay modelo neuronal |
| **Spatial Echo** (repo `3D`) | No integrado | Nada | Ver `RESEARCH_0.5.md` §1: su DSP depende de JUCE y el repositorio no tiene rama de referencia |
| **AI Choir Studio** | No localizado | Nada | `/mnt/project-files/ai-choir-studio/` no existe. Hace falta el paquete o el repositorio |

### Comandos de la API JSON

- Catálogo: `catalog` → `plugins[]`.
- Trabajos: `jobStatus {id}`, `jobCancel {id}`.
- VocalShift: `vocalPresets`, `vocalProcess {clipId, params}` (devuelve un trabajo), `clipVersion {clipId, original, match?}`
  (A/B y volumen igualado, sin paso de deshacer), `discardProcess {clipId}` (vuelve al original, con deshacer).
- Solo AI: `soloAnalyze {clipId}` (trabajo; **no** cambia el proyecto), `setHarmony {…}` (lo que el usuario confirma),
  `soloProposals {…}` (4 alternativas sin tocar el proyecto), `soloMelody {…, replaceTrackId}` (crea o sustituye la
  pista de Solo AI; nunca una pista bloqueada ni una que no sea suya).

Añadidos en la 0.6:

- VocalShift: `vocalPreview {clipId, params, fromFrame}` (trabajo: procesa unos segundos sin tocar el proyecto) y
  `previewListen {clipId, on}` (escucha la prueba en su sitio dentro de la toma). El igualado de volumen del A/B es
  solo de escucha: nunca llega a la exportación ni al proyecto guardado (corrige el defecto de la 0.5).
  El retardo de la segunda voz se aplica desde la primera muestra (en la 0.5 tardaba hasta 3 s en alcanzarse).
- Solo AI: `soloTransform {trackId, op}` con `simpler`, `intense`, `soft`, `octaveUp`, `octaveDown` (solo en su propia
  pista, nunca en una bloqueada, un paso de deshacer); `soloMelody` acepta `endingSeed` y `endingBars` para rehacer
  solo el final conservando lo anterior, incluidas las notas editadas a mano.
- Exportación: `exportDaw {path, wet, bits}` (trabajo), `exportMidi {path}`.

### Guardado

El formato de proyecto sigue en la versión 2: los campos nuevos son aditivos (`clip.process`, `harmony`), así que un
proyecto de la 0.4 se abre sin migración y uno de la 0.5 abierto con una 0.4 pierde solo esos campos. Un clip
recortado o partido después de procesarse vuelve al original (el procesado ya no corresponde a ese trozo).

## 4. Cómo añadir el siguiente plugin

1. Submódulo fijado a un commit, sin usar su CMake raíz si descarga JUCE u otras dependencias.
2. Un puente `core/src/<Nombre>Bridge.cpp` que solo exponga tipos propios de CAUCESOUND.
3. Su descriptor en `pluginDescriptors()` y, si es offline, un trabajo en `JobRunner`.
4. Pruebas: entradas extremas, cancelar, cierre de proyecto, guardado y reapertura, y la medida de latencia.
5. Solo entonces, su pantalla.

## 5. Decisiones que siguen pendientes del propietario

1. **Licencia de los núcleos propios** (Solo AI, VocalShift, Spatial Echo): ninguno declara licencia. Para distribuir
   la app hay que fijarla, aunque sea «propietaria, todos los derechos reservados».
2. Ubicación de **AI Choir Studio**.
3. Rama de referencia de **Spatial Echo** (`3D`) y permiso para separar en ese repositorio un núcleo sin JUCE.
