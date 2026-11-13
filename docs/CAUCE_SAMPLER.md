# Cauce Sampler (misión 0.9)

Reproductor de instrumentos muestreados del motor `cs_core` (C++17, sin JUCE, sin dependencias nuevas). Lo usan:

- `cs.sampler`: instrumento melódico de un paquete en una pista de instrumento (`core/src/SamplerInstrument.cpp`).
- `cs.drums` con `sampleKit`: los 8 carriles del Channel Rack tocan un kit de muestras (`core/src/Drums.cpp`).
- La escucha previa del explorador (una voz aparte en la ranura `tracks.size()`, nunca en el proyecto).

Código: `core/include/cs/Sampler.h`, `core/src/Sampler.cpp`. Pruebas: `core/tests/TestSampler.cpp`.

## 1. Auditoría previa (fase A): por qué sonaba artificial

Hechos comprobados en el código de la 0.8:

| Causa | Evidencia | Peso estimado |
|---|---|---|
| **Todo es síntesis**: Cauce Synth es sustractivo (2 osciladores + filtro + ADSR) y Cauce Drums genera cada pieza con fórmulas. No había ninguna grabación de un instrumento real. | `core/src/Synth.cpp`, `core/src/Drums.cpp` | Alto (es el origen del timbre «de juguete» en piano, bajo eléctrico y cuerdas) |
| **Un solo sonido por carril**: la fuerza (velocity) solo cambia el volumen, nunca el timbre; no hay round robin. Dos golpes iguales son idénticos bit a bit («efecto ametralladora»). | `Drums.cpp` `trigger()` | Alto en charles y cajas rápidas |
| **Sin articulaciones** (staccato, ghost, muteadas) ni pedal de sustain real en los teclados. | `Synth.cpp` | Medio |
| Composición y MIDI: humanización solo de intensidad (±5), rejilla exacta. | `Composer.cpp` `PatternBuilder` | Medio (en estilos con swing ya se compensaba) |
| Mezcla y efectos: correctos y medidos en la 0.6/0.8 (sin recortes, soft-clip transparente). No es la causa principal. | `docs/STATUS.md`, `muestras-0.8/metricas.json` | Bajo |
| Altavoces pequeños: el 808 y los subgraves casi no salen por el altavoz del móvil (por debajo de 150 Hz). | métrica `above150HzVsTotalDb` | Medio en Trap/Dark |

Inferencia razonable (no medida con oyentes): lo que más acerca el sonido a «real» es tener grabaciones reales con varias
capas de fuerza y tomas alternas. Eso es lo que añade la 0.9. **No se ha escuchado en el Samsung todavía.**

## 2. Decisión: motor propio frente a sfizz

| | Propio (elegido) | sfizz |
|---|---|---|
| Licencia | Nuestra | BSD-2 (compatible) |
| Estado | Mantenido por nosotros | **Repositorio archivado el 21-06-2026** (sin mantenimiento) |
| Dependencias | Ninguna nueva | Abseil, pugixml, etc.; sin soporte oficial de Android ni WASM documentado |
| Tamaño | ~1 500 líneas | Grande, con muchos opcodes que no usamos |
| Tiempo real | Diseñado para el contrato del motor: cero reservas en `process()` (probado) | Habría que auditarlo |
| SFZ | Subconjunto (abajo) | Casi completo |

Desventaja aceptada: solo entendemos un subconjunto de SFZ. Los opcodes desconocidos se ignoran y se informan como
avisos (no rompen la carga). Si en el futuro hace falta SFZ completo, el formato de paquete no cambia.

## 3. Qué hace

- **Mapas SFZ** (subconjunto): jerarquía `<control> <global> <master> <group> <region>`, `default_path`, `sample`
  (con espacios), `key`/`lokey`/`hikey`/`pitch_keycenter` (nombres de nota, c4 = 60), `lovel`/`hivel`,
  `seq_length`/`seq_position` (round robin) y `lorand`/`hirand` (aleatorio determinista), `tune`, `transpose`,
  `pitch_keytrack`, `volume`, `pan`, `amp_veltrack`, `ampeg_attack/hold/decay/sustain/release`,
  `loop_mode` (`no_loop`, `one_shot`, `loop_continuous`, `loop_sustain`) con `loop_start/end`, `offset`, `end`,
  `group`/`off_by` (choke: el charles abierto se corta con el cerrado), `trigger=release`.
- **Muestras**: WAV PCM 16/24/32 y float, mono o estéreo, cualquier frecuencia (se convierte a la del proyecto al cargar).
  Se guardan en int16 (la mitad de memoria que float; el ruido de cuantización queda a -96 dBFS).
- **Reproducción**: interpolación cúbica de Hermite, envolvente ADSR por voz, pedal de sustain (CC64), polifonía
  limitada por instrumento (máximo 64 voces), modo mono con legato y glide (808).
- **Robo de voces sin clics**: la voz robada se apaga en 5 ms (salto máximo por muestra medido: 0,0216).
- **Independiente del tamaño de bloque** y **sin reservas de memoria en el hilo de audio** (probado con contador de
  reservas).
- **Muestras que faltan o dañadas**: suena lo demás y se informa; un archivo que no es WAV no tumba la app.
- **Seguridad de rutas**: rechaza `..`, rutas absolutas y `:` en `sample=` (un mapa malicioso no lee fuera del paquete).

## 4. Memoria y carga (política)

- **Precarga completa** en el hilo de control al elegir el instrumento o abrir el proyecto (nunca en el hilo de audio).
  No hay streaming desde disco: con los tamaños actuales (2-40 MB por instrumento) no compensa su complejidad ni su
  riesgo de cortes en teléfonos con almacenamiento lento.
- **Presupuesto**: 160 MB de muestras residentes. Un instrumento se carga entero o no se carga (con mensaje), nunca a
  medias.
- **Caché compartida** (`SampleCache`, `weak_ptr`): dos pistas con el mismo kit comparten las muestras; al dejar de
  usarse se liberan.
- Se libera al cambiar de instrumento o cerrar el proyecto (prueba de concurrencia: 0 instancias vivas al terminar).

## 5. Proyectos y compatibilidad

- El proyecto guarda `"instrument": "<paquete>:<instrumento>"` y `"packVersion"`, **nunca rutas**.
- Formato **v4 solo si se usa** un instrumento de paquete o un kit de muestras; si no, se sigue guardando v3 byte a byte
  igual que la 0.8 (probado). Al pasar a v4 se guarda una copia `project.v3.json` del original.
- **Paquete ausente**: el proyecto se abre, la pista queda muda y señalada («Faltan sonidos de este proyecto»), y su
  estado se conserva al guardar. Nunca se sustituye por otro sonido.
- Cambiar de instrumento o de kit es un paso de Deshacer.

## 6. Rendimiento medido (x86-64 de la nube, NO en el Samsung)

- 32 voces estéreo simultáneas: 10,4 ms de CPU por segundo de audio (≈1 % de un núcleo de escritorio). En un Cortex-A
  de gama media se espera entre 3 y 6 veces más (inferencia, sin medir).
- Memoria medida con el estado del motor: kit Raw Boom Bap 1,9 MB (35 zonas); Cauce Electric Bass 8,6 MB (42 zonas).
  Cauce Grand Piano ≈ 40 MB (tamaño de sus WAV; por eso es un paquete opcional fuera del APK).
