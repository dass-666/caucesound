# VocalShift como inserción en vivo: estudio y prueba de concepto (0.9.2)

**Conclusión:** la prueba de concepto funciona en el ordenador. Declara una latencia de 150 ms, el motor la compensa y
no reserva memoria en el hilo de audio. **No se activa en la app todavía** porque nada se ha medido en un teléfono y
hay tres limitaciones de experiencia que conviene resolver antes. En la app, VocalShift sigue siendo un procesado de la
grabación, no destructivo y fuera de tiempo real (el original no se toca y se compara A/B). El navegador de plugins
lo muestra así: «Procesar la grabación (no en vivo)».

## Qué se construyó

- `cs.vocalshift.live` (`core/src/VocalBridge.cpp`, clase `VocalShiftLivePoC`): un `AudioProcessor` que envuelve
  `::vs::VocalChain`, el mismo motor DSP que el procesado offline.
  - `prepare()` reserva todo en el hilo de control y deja los suavizados en su valor inicial, como en el modo offline.
  - `latencySamples()` devuelve la latencia real de la cadena: **7200 muestras = 150 ms a 48 kHz**.
  - Los parámetros (tono, carácter, magia, segunda voz y su nivel) son atómicos. `setParameter` marca un indicador y el
    hilo de audio aplica el cambio al principio del bloque siguiente, sin bloqueos.
- Está registrado en `createProcessor()` pero **no** en `effectTypes()`. Por eso no aparece en el catálogo y
  `addInsert` lo rechaza. Una prueba lo comprueba.

## Pruebas (`core/tests/TestVocalLive.cpp`, x86-64, señal sintética)

| Prueba | Resultado |
|---|---|
| Oculto: no está en el catálogo y `addInsert` lo rechaza | Superada |
| Latencia declarada | 7200 muestras (150,0 ms) |
| Alineación con PDC durante la reproducción, con bloques de 192 y 256 | Desfase de **0 muestras** frente a la pista seca y sin audio adelantado antes del clip |
| Reservas de memoria en `process()` durante 10 s con cambios de tono en marcha | **0** |
| CPU en esta máquina (x86 en la nube) | Unos 26-30 ms por segundo de audio estéreo, es decir **2,6-3 % de un núcleo** |

Una medición intermedia encontró 60 reservas, pero venían de `setParameter()`, que construye la lista de parámetros. Esa
llamada se hace en el hilo de control, igual que en los demás efectos, así que no afecta al audio. La prueba deja de
contarla y mide solo `process()`.

## Por qué no se activa todavía

1. **CPU en ARM sin medir.** El 3 % de un núcleo x86 no se traslada directamente a un Samsung de gama media. Como
   estimación sin verificar, podría ser 3-6 veces más caro. Con dos o tres pistas vocales y el sampler sonando, el
   margen podría agotarse y oírse cortes. **Qué falta:** ejecutar `cs_bench` en el teléfono, o añadir una medición en
   la app, y fijar un límite de instancias.
2. **Pre-roll al pulsar Play o al saltar.** El PDC por pista lee el contenido 150 ms por delante. Si se pulsa Play
   justo antes de una frase, el motor necesita 150 ms de «calentamiento». La prueba confirma que no hay desfase ni
   pre-eco, pero no comprueba cómo suena el arranque del efecto (silencio o ataque blando) en la primera palabra.
   **Qué falta:** prerrellenar la cadena al buscar posición (seek) y una prueba de escucha.
3. **Sin monitorización de la entrada.** Con 150 ms de latencia no sirve para oírse mientras se canta: el retardo
   descoloca a cualquiera. Solo serviría para la reproducción, que es lo que estudia esta prueba de concepto.
4. **Buses bloqueados.** Los buses no tienen PDC, y `addInsert` ya rechaza efectos con latencia en un bus. Se mantiene
   así.
5. **Cambiar parámetros en vivo** funciona sin reservas de memoria. Falta comprobar al oído si los saltos de tono
   producen artefactos audibles (en el modo offline los parámetros no cambian durante el render).

## Qué haría falta para activarlo (propuesta, no implementado)

1. Medir la CPU en el Samsung de Diego y decidir un límite, por ejemplo 2 instancias en vivo.
2. Prerrellenar la cadena en `seek` para eliminar el arranque.
3. Añadirlo a `effectTypes()` con la categoría «Procesamiento vocal», solo en pistas de audio, con la etiqueta
   «latencia 150 ms, compensada» y un aviso de que no sirve para monitorizar.
4. Una prueba de escucha en el teléfono antes de anunciarlo.

Coste estimado: 1-2 sesiones de trabajo una vez exista la medición del teléfono. **Recomendación:** hacerlo después de
que Diego valide la 0.9.2 en el teléfono, no antes.

## Avance de la 0.10 (sigue experimental y oculto)

Esto trata de **procesar una pista ya grabada mientras suena la canción**. No es monitorización del micrófono: con
150 ms de latencia no sirve para oírse al cantar, y eso no ha cambiado.

Lo que se encontró y se corrigió (todo en x86-64, con una voz sintética; nada en un teléfono):

| Problema | Causa | Qué se hizo | Prueba |
|---|---|---|---|
| Al pulsar Play en mitad de una frase, los primeros 150 ms de la pista procesada salían en silencio | El PDC ya leía 150 ms por delante, pero el efecto arrancaba vacío | El motor «ceba» las pistas con latencia al arrancar: procesa por adelantado el tramo anterior y descarta esa salida (`Engine::primeLatentTracks`) | Nivel de los primeros 150 ms: −15,4 dB, igual que en reproducción continua (antes, silencio). Diferencia con la reproducción continua a partir de 300 ms: −49,8 dB |
| Al saltar durante la reproducción sonaba una cola del sitio anterior | El efecto guardaba 150 ms de audio del punto de origen | En cada salto se reinicia y se ceba de nuevo | Salto a una zona vacía: −180 dB en los 200 ms siguientes |
| La exportación perdía el principio de una pista con latencia | Mismo motivo | La exportación también ceba | Primeros 100 ms a −0,1 dB del original |
| Cambiar parámetros mientras suena | Sin medir hasta ahora | Se midió: el motor de VocalShift ya suaviza los cambios | Seis cambios (tono, segunda voz, nivel, magia, carácter): el mayor salto entre muestras es 0,7-0,9 veces el de la señal estable. No hay clics; **si el cambio suena natural hay que escucharlo** |
| Sobrecarga | No había protección | Si el efecto gasta de forma sostenida más de la mitad del tiempo real, pasa a la señal seca con el mismo retardo (sigue alineada) con un fundido de 10 ms. Se rearma al volver a pulsar Play | Señal seca exacta (−120 dB de diferencia), sin clic |
| Demasiadas instancias | Sin límite | Máximo 2 por proyecto y solo en pistas de audio, detrás de un interruptor de pruebas (`setExperimental`), apagado por defecto y sin botón en la app | El tercero se rechaza; el catálogo sigue sin ofrecerlo |

**Coste del arranque (lo que queda por resolver):** cebar 300 ms de audio de golpe tardó 6,4-10 ms en x86-64, y un
bloque de audio dura 4 ms. Es decir, el bloque del arranque ya se pasa de tiempo en un ordenador. En un teléfono será
peor (inferencia: 3-6 veces). Consecuencia probable: un pequeño corte justo al pulsar Play o al saltar cuando hay una
pista con VocalShift en vivo. Soluciones posibles, sin implementar: repartir el cebado en varios bloques mientras el
transporte está parado, o arrancar con la pista seca y fundir al efecto.

**Medición en el teléfono:** Ajustes → «Diagnóstico (experimental)» → «Medir VocalShift en vivo (5 s)». No activa el
efecto ni toca el proyecto: procesa una voz sintética en bloques de 192 muestras y enseña el porcentaje de un núcleo,
el peor bloque y lo que cuesta el arranque. En x86-64 dio 2,8 % de un núcleo, peor bloque 8 % y arranque de 10 ms.
Con esas tres cifras del Samsung se decide si se activa, con cuántas instancias y si hace falta repartir el arranque.

## Catálogo futuro (estudio; nada de esto se anuncia en la app)

El navegador de plugins de la 0.9.2 separa tres tipos de herramientas, y conviene mantener esa separación:

- **Instrumentos** (generan sonido a partir de notas): sintetizadores internos y Cauce Sampler. Se eligen en la pista,
  no en el Mixer.
- **Inserciones** (procesan el audio de un canal en tiempo real): EQ, compresor, reverb, delay, saturación, etc. Viven
  en la cadena del canal del Mixer.
- **Herramientas generativas o de procesado offline** (crean o procesan contenido una vez): Solo AI (crea MIDI) y
  VocalShift (procesa una grabación). El navegador las muestra en secciones propias y abren su propio panel.

Estado de los candidatos:

- **Spatial Echo** (repo `dass-666/3D`): está hecho con JUCE. El motor móvil no usa JUCE, así que habría que portar el
  DSP a C++ puro o aceptar JUCE solo para ese módulo, con su licencia y su peso. Tiene la misma decisión pendiente que
  en la 0.5. No se tocó.
- **AI Choir Studio**: su repo sigue sin localizar. No se puede estimar nada.
- **Nuevos efectos Cauce**: el catálogo (`effectTypes()` + `catalog.plugins`) ya admite categorías nuevas. Una
  categoría desconocida aparece en «Otros», así que añadir un efecto no requiere tocar la interfaz. La categoría
  «Modulación» (chorus o flanger) no se muestra porque todavía no existe ningún efecto de modulación.
- **Presets y cadenas**: ya existen las cadenas completas (`applyFxChain`). El navegador las ofrece en «Cadenas». Un
  siguiente paso razonable sería guardar cadenas propias del usuario.
