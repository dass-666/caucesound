# Hotfix 0.9.2: Variar y Regenerar, plugins en el Mixer y grabación de un toque

Rama `claude/project-thread-sol38i`, basada en la 0.9.1 (PR #7). versionCode 14, `0.9.2-hotfix-preview`, misma firma
Preview. **Nada se ha probado en un teléfono.** Todo lo que aparece como «probado» se ejecutó en la nube, en Linux
x86-64: el motor C++ y la app en la JVM con Robolectric.

## 1. «Variar todo», «Regenerar beat» y los botones de cada parte «no hacen nada»

### Cómo se rastreó

El recorrido completo es botón → `DawViewModel` → JNI (`NativeEngine.command`) → `handleCommand` → `Session` →
compositor (`MusicPlan`, semillas, candados) → clips MIDI y patrones → grafo de audio → reproducción → estado de la
interfaz. Se reprodujo de dos maneras: en el motor con el mismo JSON que envía la app, y en la JVM con el
`JniBridge.cpp` real, que sale del `DawViewModel` de verdad.

### Causa real: cuatro problemas que se suman

Hechos comprobados:

1. **La interfaz no daba ninguna señal.** El comando llegaba, devolvía `ok` y cambiaba las notas, y el grafo se
   republicaba al instante, también durante la reproducción. Pero la pantalla no mostraba indicador de trabajo,
   versión ni mensaje. Si el beat estaba parado, no se oía nada hasta pulsar Play. El resultado visible era «no pasa
   nada».
2. **Algunos botones no hacían nada de verdad.**
   - Variar y Rehacer **Pad** nunca cambiaban el pad: el pad no dependía de la semilla.
   - **Rehacer Bajo** no hacía nada en Pop ni en Dark Hip-hop: esos estilos tenían un único patrón de bajo.
   - **Rehacer Acordes** en Dark Hip-hop cambiaba muy poco.
3. **En Electrónica, «Variar» tocaba sobre todo el bombo y el bajo.** Por encima de 300 Hz, que es lo que reproduce el
   altavoz de un móvil, el cambio era el más bajo de todos los estilos: 0,24-0,26, frente a ≥0,28 en los demás.
   Inferencia razonable: en el altavoz del Samsung, Electrónica era el estilo donde menos se notaba.
4. **Nada garantizaba un cambio significativo.** Una semilla nueva podía dar un resultado casi igual.

### Corrección

- **Motor** (`Session::variationLocked`): las tres operaciones comparten un solo camino.
  - Intenta hasta 8 semillas y mide el cambio real de eventos en cada pista (Jaccard sobre inicio y tono, y sobre los
    pasos de batería).
  - Acepta la primera semilla que supera un umbral: Rehacer 15 %, Variar con semilla nueva 25 %, Regenerar 40 %. Si
    ninguna lo supera, se queda con la mejor.
  - Si no encuentra ningún cambio, o si todas las pistas están bloqueadas, devuelve un error comprensible:
    - «quita el candado para variarlas»;
    - «no se encontró una versión distinta de la actual; prueba otra parte o «Regenerar»».
  - Cada éxito guarda un punto de Deshacer, sube la **revisión** del plan (se guarda en el proyecto) y devuelve un
    informe (`variation`) con lo que cambió en cada pista y las pistas bloqueadas que se respetaron.
- **Compositor**: el pad y el bajo dependen ahora de la semilla. Dark Hip-hop tiene ritmos de acordes propios y la
  melodía electrónica puede saltar de octava. **Las semillas por defecto dan exactamente el sonido anterior**, así que
  las plantillas suenan igual y los proyectos anteriores se abren sin cambios.
- **Interfaz** (`VariationsPanel`):
  - una tarjeta de versión con «Generando…» mientras trabaja, «Versión N», el porcentaje de cambio por parte, las
    partes bloqueadas y el botón **Escuchar**;
  - si está parado, Escuchar reproduce desde la sección A; si ya está sonando, el cambio se oye al momento;
  - los botones se desactivan mientras trabaja y un doble toque no lanza dos variaciones;
  - Anterior y Siguiente para comparar, y Deshacer y Rehacer exactos.

### Mediciones después del arreglo

Cambio de notas y golpes, medido de extremo a extremo en la JVM desde el botón, con el JNI real:

| Estilo | Variar todo | Regenerar beat |
|---|---|---|
| Trap | 46 % | 78 % |
| Reggaeton | 44 % | 73 % |
| Hip-hop | 40 % | 72 % |
| Boom Bap | 29 % | 73 % |
| Jazz Rap | 36 % | 85 % |
| Dark Hip-hop | 35 % | 70 % |
| Pop | 43 % | 81 % |
| Electrónica | 43 % | 79 % |

En audio por encima de 300 Hz, con el botón pulsado durante la reproducción, todas las operaciones superan el umbral
de la prueba: Rehacer ≥0,25 y Regenerar ≥0,40 de diferencia.

### Pruebas que lo cubren

- `core/tests/TestVariations.cpp`:
  - cada botón en los 8 estilos: revisión +1, cambio mínimo, Deshacer y Rehacer exactos;
  - el cambio se oye durante la reproducción por encima de 300 Hz;
  - los candados se respetan y los fallos se explican;
  - persistencia, y los planes antiguos siguen abriendo.
- `android/.../EndToEndVariationsTest.kt` (JNI real en la JVM):
  - el flujo completo de Electrónica: cada botón, candado, Deshacer y Rehacer, guardar, cerrar, reabrir y Play;
  - los 8 estilos;
  - la grabación rápida sin acompañamiento.
- `android/.../ui/Hotfix092UiTest.kt`: la tarjeta de versión, el indicador de trabajo, los botones desactivados, el
  navegador de plugins y el diálogo de grabación.

Para ejecutar las pruebas de extremo a extremo en la JVM:

```
cmake -S . -B build-hostjni -DCMAKE_BUILD_TYPE=Release -DCS_BUILD_HOST_JNI=ON -DCS_BUILD_TESTS=OFF
cmake --build build-hostjni --target caucesound
cd android && CS_HOST_JNI_DIR=$PWD/../build-hostjni ./gradlew testDebugUnitTest
```

Sin `CS_HOST_JNI_DIR`, esas pruebas se omiten (no fallan). La CI ya construye la biblioteca antes de las pruebas.

## 2. Plugins en el Mixer, con un flujo similar al de un DAW de escritorio

El recorrido es: Mixer → tocar un canal → su cadena de efectos → «+ Agregar plugin» → navegador por categorías → los
controles se abren solos → ajustar escuchando → activar, desactivar, reordenar o borrar. Se reutilizan `InsertRack`,
`EffectPicker` y las cadenas; no hay un sistema paralelo.

- Categorías: EQ y filtros, Compresión y dinámica, Reverb y espacios, Delay, Saturación y distorsión, Procesamiento
  vocal y Herramientas musicales. **Solo se muestra lo que está implementado.** «Modulación» no aparece porque todavía
  no existe ningún efecto de modulación. Una categoría desconocida cae en «Otros».
- **VocalShift** aparece en las pistas de audio como «Procesar la grabación (no en vivo)» y abre su panel con el último
  clip. No se presenta como efecto en vivo.
- **Solo AI** aparece en «Herramientas generativas» y abre su panel.
- **Cadenas completas**: tienen su propia sección.

El estudio de VocalShift como inserción en vivo está en `VOCALSHIFT_LIVE_STUDY.md`. Hay una prueba de concepto que
funciona en el ordenador, pero queda oculta hasta medirla en un teléfono.

## 3. Grabación de un toque

- Si no hay ninguna pista de audio armada, el botón rojo abre un diálogo para crear una pista «Voz» o usar una
  existente, la arma y empieza a grabar.
- **Grabar sin acompañamiento**: es una opción del diálogo y también un interruptor en el modo de grabación. Silencia
  el beat y el metrónomo **solo durante la toma**. La cuenta atrás sigue sonando y no se toca la mezcla ni el proyecto.
  Al parar, todo suena como antes.
- El modo multipista sigue igual: si hay pistas armadas, el botón rojo graba directamente.

## Qué funciona hoy y qué queda para después

| Hoy, probado en la nube | Para después |
|---|---|
| Cambio garantizado y medido en los 8 estilos, versión, Escuchar, comparar, Deshacer y candados | Comprobar al oído en el Samsung que el cambio se nota en el altavoz |
| Navegador de plugins por categorías y VocalShift offline visible | VocalShift en vivo (CPU en ARM, pre-roll), Spatial Echo (JUCE), AI Choir (repo sin localizar), modulación |
| Grabación de un toque y sin acompañamiento | Validar en el teléfono el micrófono, los permisos y la latencia real |
