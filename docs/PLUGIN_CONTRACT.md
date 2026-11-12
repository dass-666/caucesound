# Contrato de plugins de CAUCESOUND (misión 0.4, etapa C)

Estado: **diseño**. Este documento no integra todavía ningún plugin. Las secciones marcadas «Hecho» existen en el
motor y tienen pruebas; el resto es propuesta. No se ha copiado ni modificado código de los repositorios de plugins.

## 1. Qué ya ofrece el motor (hecho, con pruebas)

| Requisito del contrato | Dónde | Estado |
|---|---|---|
| Identificador estable, parámetros con rango, estado guardable y validado | `cs::AudioProcessor` (`typeId`, `parameters`, `saveState`/`loadState`) | Hecho |
| Latencia declarada y compensación (PDC) por pista | `latencySamples()` + `Engine` (lectura adelantada) | Hecho en pistas. **Falta en buses**: un bus con una inserción latente no se compensa todavía |
| Cola (reverb, delay) en exportación | `tailSamples()` | Hecho |
| Instrumentos con MIDI exacto a la muestra | `cs::Instrument::handleMidi`, `allNotesOff`, `activeVoices` | Hecho |
| Transporte para el plugin (tempo, compás, posición) | `cs::TransportInfo` definido | Falta pasarlo por bloque |
| Añadir, quitar, bypass, ordenar, deshacer, guardar y restaurar | `Session::addInsert`…, formato de proyecto | Hecho (lo usan los 7 Cauce FX) |
| Tiempo real: sin reservas ni bloqueos en `process()` | Grafo inmutable publicado atómicamente; prueba de 600 ediciones con audio sonando | Hecho para los procesadores propios; cada plugin externo debe demostrarlo con sus pruebas |

## 2. Lo que añade el contrato

```cpp
// Propuesta (no implementada todavía)
struct PluginDescriptor {
    std::string id;          // "solo.ai", "vocalshift", "spatial.echo"
    std::string version;     // versión semántica del núcleo integrado + commit fijado
    enum class Kind { Effect, Instrument, OfflineTool } kind;
    bool realtime;           // false = solo procesamiento offline (no se ofrece como inserción en vivo)
    int  latencySamples;     // latencia fija conocida al preparar
    bool wantsMidi, wantsTransport;
    size_t maxMemoryBytes;   // límite declarado; se comprueba en las pruebas
};

// Trabajo pesado fuera del hilo de audio: análisis, generación, render offline.
class OfflineJob {
public:
    virtual ~OfflineJob() = default;
    virtual void run (std::atomic<float>& progress, const std::atomic<bool>& cancel) = 0; // hilo de trabajo
    // Resultado siempre editable: notas MIDI (setClipNotes) o un clip de audio nuevo (como una importación).
};
```

Reglas:
1. Un plugin `realtime = false` **nunca** aparece como inserción en vivo. La interfaz lo presenta como «Procesar»
   con vista previa y comparación original/procesado. No se presenta el procesamiento offline como tiempo real.
2. Los resultados offline son **no destructivos**: el audio original se conserva y el procesado entra como clip nuevo
   (o como notas MIDI editables). Deshacer lo quita.
3. Si una integración no está lista, no hay botón ni pantalla para ella.

## 3. Plan por plugin

| Plugin | Lo que se sabe (comprobado) | Integración propuesta | Bloqueos |
|---|---|---|---|
| **Solo AI** (`solo_core`, C++17, sin JUCE, sin dependencias) | API offline completa: `BeatAnalyzer` da BPM y tonalidad **con confianza** (`HarmonyTimeline`), `MelodyExplorer` da 4 propuestas de melodía. 143/144 pruebas en ARM64 (QEMU) | Flujo «Generar melodía o solo» como `OfflineJob`: analiza o usa el plan de la canción; si la confianza de tonalidad es baja, pide la escala; el resultado va al piano roll como clip MIDI. Progreso y cancelar | Licencia y mecanismo de dependencia (§4). La prueba numérica que difiere en ARM (sobremuestreo de la guitarra) no afecta al flujo MIDI, pero sí al sonido de guitarra |
| **VocalShift** (`vs_core`, C++17, Signalsmith MIT) | `VocalChain::process` en el sitio, latencia fija 150 ms. 57/59 pruebas en ARM64 (las 2 que fallan son umbrales de CPU bajo emulación). Coste en x86: 1,5-2,5 % de un núcleo el pitch, 27-33 % con segunda voz | Primero **offline** (procesar una toma de voz, comparar original/procesado). En vivo solo si la medida en el Samsung lo permite, y con aviso de 150 ms de latencia al monitorizar | Licencia, dependencia, **medir CPU en el teléfono**, PDC en buses |
| **Spatial Echo** (repo `3D`) | El núcleo usa `juce::` (buffers, hilos, JSON). JUCE no se incorpora sin decidir su licencia | Adaptador: portar `ses::` a tipos propios (sustituir `juce::AudioBuffer`, `juce::Thread`, `juce::var`) o mantenerlo fuera de la app | Trabajo de portado no estimado con precisión; decisión de licencia JUCE |
| **AI Choir Studio** | No encontrado. No aparece entre los repositorios accesibles; los candidatos MONTAMBRE, RUN y ek-chuah no son legibles con el acceso actual | Mismo contrato que Solo AI (`OfflineJob` que produce MIDI o audio de armonías) | **Hace falta que el propietario diga dónde está** o dé acceso |

## 4. Decisiones que necesita el propietario

1. **Licencia de los núcleos propios** (Solo AI, VocalShift, Spatial Echo): ninguno declara licencia. Para
   distribuir la app hay que fijarla (aunque sea «propietaria, todos los derechos reservados»).
2. **Mecanismo de dependencia**. Recomendación: **submódulo git fijado a un commit** concreto de cada repositorio
   (no se copia código, la versión queda registrada y se actualiza a propósito). Alternativa: copia versionada en
   `third_party/` (más simple de compilar, pero duplica código y se desincroniza).
3. Ubicación de **AI Choir Studio**.
