# Investigación de la misión 0.5 (2026-11-12)

Este documento recoge lo que se **investigó pero no se implementó**, con los motivos. Cada apartado separa lo
comprobado (leído en el código o medido) de lo que es inferencia.

## 1. Spatial Echo Studio (repositorio `dass-666/3D`)

**Comprobado** (clon local de solo lectura; no se modificó nada):

- `main` (`f3d521e`) solo tiene un esqueleto de 20 archivos. El trabajo real está en dos ramas sin fusionar:
  `claude/b2-wav-echo` (`40ab1dd`) y `claude/project-thread-3poa1c` (`4532b50`, 104 archivos). No hay rama de referencia.
- No hay archivo de licencia.
- Núcleo de audio: `EchoEngine.cpp` (1066 líneas), `LiveEchoes.cpp` (501), `PointFx.cpp` (457), `PitchRenderer.cpp`
  (185), `LiveChain.cpp` (134). Usa Signalsmith Stretch (MIT), igual que VocalShift.
- Su CMake descarga JUCE. JUCE se distribuye bajo AGPLv3 o licencia comercial: incorporarlo a una app cerrada exige
  pagar la licencia o publicar el código de la app. Por eso CAUCESOUND no lo usa.
- El uso de JUCE dentro del DSP es acotado:

| Qué usa de JUCE | Dónde | Sustituto en C++ puro |
|---|---|---|
| `AudioBuffer`, `jmin/jmax/jlimit`, `Decibels`, `MathConstants` | Todo el núcleo | Tipos propios triviales (un día de trabajo, mecánico) |
| `SmoothedValue` | Motores de eco | Rampa lineal propia (ya existe en `cs_core`) |
| `juce::Reverb`, `IIRFilter` | `PointFx.cpp` | **Aquí cambia el sonido**: habría que reescribir la reverb (por ejemplo con la de CAUCESOUND) y los filtros. El resultado no sonará idéntico al plugin original |
| `String` (53 usos) | `Scene.h` | `std::string` |
| `Thread`, `CriticalSection`, `WaitableEvent` | `PitchRenderer.cpp` | `std::thread` y `std::mutex`, o mejor el `JobRunner` de CAUCESOUND |
| `File`, `Result` | `OfflineRender` | No hace falta: CAUCESOUND ya lee y escribe WAV |

**Recomendación** (propuesta, no hecha): separar **dentro del repositorio `3D`** una biblioteca `ses_core` sin JUCE
(como ya tienen VocalShift con `vs_core` y Solo AI con `solo_core`), con sus pruebas, y después fijarla como submódulo.
Ventaja: un solo código para el plugin de escritorio y para la app. Desventaja: toca el repositorio original, que hoy
no estoy autorizado a modificar. La alternativa (un adaptador dentro de CAUCESOUND que reimplemente los tipos de JUCE)
es más rápida pero deja dos versiones del mismo DSP que se desincronizan.

**Estimación** (inferencia, sin haber compilado nada): entre 3 y 6 sesiones de trabajo, sobre todo por la reverb y por
las pruebas de equivalencia de sonido. No hay interfaz de Spatial Echo en la app porque no hay procesamiento real.

**Necesito de Diego**: qué rama es la buena, permiso para crear `ses_core` en `3D` (en una rama aparte) y la licencia.

## 2. AI Choir Studio

**Bloqueado.** `/mnt/project-files/ai-choir-studio/` no existe en la carpeta del proyecto y el paquete
`AI_Choir_C7_PAQUETE_COMPLETO.zip` no está en ninguna ruta accesible. No aparece entre los repositorios a los que
tengo acceso. No se ha escrito nada: ni código ni interfaz. Hace falta que Diego suba el zip al proyecto o indique el
repositorio. Cuando esté, entra por el mismo contrato (trabajo offline que produce audio o MIDI).

## 3. Congelar pista (Freeze Track): pospuesto

Congelar es renderizar a audio una pista de sinte terminada para ahorrar CPU. **No se ha hecho**, por tres razones:

1. No hay ninguna medida en el teléfono que diga que hace falta. En x86, el proyecto de referencia (trap + pad +
   5 efectos) usa un 6,5 % del presupuesto de 4 ms en el percentil 99, y 32 pistas un 2,7 %. Un teléfono es más lento,
   pero el dato real no existe todavía.
2. Cambia el formato del proyecto (una pista con dos estados) y cómo se deshace. Es un cambio que conviene hacer una
   sola vez y bien.
3. La infraestructura que necesita ya existe desde esta misión: trabajo offline cancelable y clip con versión A/B.
   Hacerlo después costará menos que antes de la 0.5.

Decisión propuesta: medir primero en el Samsung (prueba 48 y la nueva 58). Si hay cortes con proyectos normales, es
la siguiente tarea de motor.

## 4. PDC en buses

Estado: **prohibido de forma explícita**, no simulado (ver `PLUGIN_CONTRACT.md` §2). La solución real: calcular la
latencia máxima entre buses, retrasar esa cantidad la señal seca de todas las pistas y la de los buses más rápidos, y
sumar ese retraso a la lectura adelantada del transporte. Es un cambio en el grafo de render que afecta a todo el
audio, y hoy ningún efecto lo necesita. Se hará cuando exista un efecto latente pensado para buses.

## 5. VocalShift: qué se puede y no se puede afirmar sobre la naturalidad

**Medido**: latencia exacta (7200 muestras), alineación tras compensar (0 muestras de error), cola de 0,76-0,78 s, la
segunda voz sube y baja de nivel de forma monótona con su control, silencio de entrada da silencio de salida, y no hay
valores no finitos ni saturación con parámetros extremos.

**No medido ni medible aquí**: si suena natural. Las pruebas usan una señal sintética con armónicos parecidos a una
voz, no una voz humana. Lo que se sabe por diseño del motor (inferencia razonable):

- Es un cambiador de tono por vocoder de fase con conservación de formantes (Signalsmith Stretch). Entre ±3 y ±4
  semitonos suele sonar creíble; a +7 (el preset de referencia) la voz principal suena procesada. Por eso el preset
  usa +7 para la **segunda voz** mezclada a −12 dB, que es donde ese carácter funciona.
- Consonantes y transitorios se emborronan algo con desplazamientos grandes; es propio de la técnica.
- No es un conversor de voz neuronal y no convierte una voz en otra persona. La app no lo promete.

La valoración real es la prueba física 52: grabar voz, procesar y escuchar con auriculares.

## 6. Sonido 2.0: lo que se intentó y se descartó

Objetivo: que el 808 se oiga mejor en altavoces pequeños (que no reproducen por debajo de ~150 Hz). Se probaron dos
generadores de armónicos en el sinte. El primero solo añadía el segundo armónico (110 Hz, todavía bajo el umbral); el
segundo **empeoró** la energía por encima de 150 Hz (de −12,5 a −13,6 dB). Los dos se retiraron y el sinte quedó como
en la 0.4. Se conserva la prueba `cauce_808_keeps_harmonics_above_its_fundamental_for_small_speakers`, que fija el nivel
actual (tercer armónico a −10,4 dB) para que no empeore. Consecuencia honesta: **los tres estilos suenan igual que en
la 0.4**. La mejora de sonido de esta misión es funcional (Solo AI sobre el beat, VocalShift), no de timbre.

## 7. Rendimiento medido (x86, no es un teléfono)

| Medida | Resultado |
|---|---|
| `cs_bench`, 8 / 16 / 32 pistas de audio, p99 | 0,62 % / 1,09 % / 2,66 % de un bloque de 4 ms |
| `cs_bench`, trap + pad + 5 efectos, p99 | 6,48 % |
| VocalShift, 3 s de voz con segunda voz | 0,30 s (10 veces más rápido que el tiempo real) |
| Solo AI, análisis de 42 s de beat | 0,25 s |
| Solo AI, una melodía de 8 compases | 0,2-0,3 ms |
| Cerrar un proyecto con un procesado en marcha | 14 ms |

Un Samsung de gama media puede ser entre 3 y 10 veces más lento (estimación genérica, **no medida**). Con ese margen,
VocalShift tardaría entre 1 y 3 s por cada 3 s de voz: aceptable para un procesado offline con barra de progreso.
