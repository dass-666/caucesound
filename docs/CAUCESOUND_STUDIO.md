# CauceSound Studio (Windows): análisis y base (misión 0.6)

**Estado: análisis más un prototipo de línea de comandos.** No existe un DAW de escritorio. Lo que existe es el
programa `cauce` (`desktop/cli/main.cpp`), que compila el mismo motor del teléfono en un ordenador y abre, renderiza,
exporta y verifica proyectos. **Solo se ha compilado y probado en Linux x86-64**: en esta sesión no había Windows ni
compilador cruzado para Windows, así que «funciona en Windows» es una inferencia razonable (C++17 estándar,
`std::filesystem`, las pocas llamadas de sistema ya tienen rama `_WIN32`), no un hecho comprobado.

## 1. Qué se reutiliza tal cual

El motor (`core/`, unas 8 600 líneas) no depende de Android: no incluye Oboe, JNI ni nada del sistema salvo archivos e
hilos estándar. Android es una capa fina encima (`android/app/src/main/cpp`: `AudioIO` con Oboe y `JniBridge`).

| Componente | Reutilizable en Windows | Nota |
|---|---|---|
| Modelo y formato `.caucesound` (`Project`, `Archive`) | Sí, sin cambios | Mismo archivo en los dos lados: es la base de Cauce Link |
| Motor de audio (`Engine`, `RenderGraph`, efectos, sintetizadores, batería) | Sí | Ya se ejecuta en x86-64 en todas las pruebas |
| `Session` y la API JSON (`cs::handleCommand`) | Sí | La interfaz de escritorio hablaría con el motor igual que la de Android |
| Deshacer, autoguardado, trabajos offline (`JobRunner`) | Sí | |
| Compositor, variaciones, Solo AI, VocalShift | Sí | Son C++ puro; los submódulos ya compilan en escritorio |
| Exportación (mezcla, stems, MIDI, paquete para DAW) | Sí | Es lo que hace hoy `cauce daw` |
| Entrada y salida de audio | **No**: hay que escribirla | §3 |
| Interfaz (Compose) | **No** | §4 |
| Lógica de pantallas en Kotlin (geometría del Piano Roll, variaciones de batería) | Parcial | §5 |

## 2. Abrir un `.caucesound` en Windows

Ya resuelto a nivel de motor. Con el prototipo:

```
cauce info   cancion.caucesound            pistas, tempo, duración, plugins que usa
cauce render cancion.caucesound mezcla.wav
cauce daw    cancion.caucesound salida.zip [--dry] [--bits 16|24|32]
cauce midi   cancion.caucesound salida.mid
cauce verify salida.zip                    comprueba hashes y que la suma de stems da la mezcla
cauce new    trap nueva.caucesound [--solo]
```

Probado automáticamente en Linux: el render de `cauce` coincide con el del motor de las pruebas porque es el mismo
código. Pendiente de comprobar en Windows: rutas con acentos y espacios (el motor pasa las rutas como texto UTF-8 a `std::filesystem` y a `fopen`, y Windows
interpreta ese texto con la página de códigos local: una carpeta «Canción» fallaría hasta adaptar esas llamadas), archivos de
más de 4 GB y el guardado seguro (renombrar encima de un archivo abierto se comporta distinto que en Linux).

**Primer paso recomendado, barato:** añadir a la integración continua un trabajo `windows-latest` que compile `cauce` y
ejecute `cs_core_tests`. Cuesta 0 $ (los minutos de Actions para repositorios privados tienen cuota gratuita, y los de
Windows cuentan doble: hay que vigilarla) y convierte la inferencia en un hecho. No se ha hecho en esta misión para no
arriesgar el CI en verde de la entrega del APK.

## 3. Audio en escritorio: WASAPI y ASIO

| Opción | Latencia típica | Licencia | Comentario |
|---|---|---|---|
| WASAPI compartido | 10 a 30 ms | API de Windows, sin coste | Funciona en cualquier equipo. Suficiente para componer y mezclar |
| WASAPI exclusivo | 3 a 10 ms | Igual | Bloquea el dispositivo para otros programas |
| ASIO | 2 a 10 ms | SDK de Steinberg; desde 2025 también bajo GPLv3 además de la licencia propietaria gratuita | Lo que esperan los usuarios con interfaz de audio. Requiere aceptar la licencia de Steinberg |

Bibliotecas que envuelven esto, para no escribir WASAPI a mano:

| Biblioteca | Licencia | Valoración |
|---|---|---|
| **miniaudio** (un solo archivo `.h`) | Dominio público o MIT-0 | **Recomendada para empezar**: WASAPI, sin dependencias, coste 0. No trae ASIO |
| RtAudio | MIT | WASAPI y ASIO (ASIO exige el SDK de Steinberg) |
| PortAudio | MIT | Igual que RtAudio, más veterana y más pesada |
| JUCE | AGPLv3 o de pago | Lo trae todo (audio, interfaz, VST3), pero obliga a abrir el código o a pagar. **Descartada** mientras el presupuesto sea 0 $ y el código sea privado |

*Los datos de licencias son los que recuerdo y deben releerse en la fuente al empezar; no se descargó ninguna de estas
bibliotecas en esta misión.*

El motor ya tiene la forma que necesitan: `Engine::process(salida, frames)` se llama desde el hilo de audio y no
reserva memoria ni bloquea. La capa de Windows sería el equivalente de `AudioIO.cpp` (unas 250 líneas en Android).

Propuesta: WASAPI con miniaudio primero; ASIO después, solo si alguien lo pide con una interfaz de audio delante.

## 4. Arquitectura visual

Restricción real: Compose para Android no se ejecuta en Windows. Opciones:

| Opción | Reutiliza la interfaz actual | Coste | Riesgo |
|---|---|---|---|
| **Compose Multiplatform (escritorio, JVM)** | Mucho: mismas pantallas en Kotlin, adaptando tamaños | Medio | Rendimiento de lienzos grandes (línea de tiempo con muchas pistas); el motor se cargaría por JNI como en Android |
| Qt (C++) | Nada | Alto | Licencia LGPL: usable con enlace dinámico, con obligaciones |
| Dear ImGui u otra interfaz inmediata en C++ | Nada | Medio | Aspecto de herramienta, no de producto |
| JUCE | Nada | Alto | Licencia (ver §3) |

Recomendación: **Compose Multiplatform**, porque es la única que no obliga a escribir dos veces el Piano Roll, la
batería, el mezclador y los paneles de plugins, y porque la frontera con el motor ya es JSON (`handleCommand`), que no
depende de la plataforma. Antes de comprometerse hay que hacer una prueba de rendimiento concreta: línea de tiempo con
40 pistas y 500 clips a 60 imágenes por segundo en un portátil corriente. Si no llega, la alternativa es dibujar la
línea de tiempo con Skia directamente y dejar el resto en Compose.

Un DAW de escritorio no es la app del teléfono en grande: necesita ventanas redimensionables, atajos de teclado,
ratón con rueda y arrastre, varios paneles a la vez. Eso es diseño nuevo, no adaptación.

## 5. Piano Roll

El Piano Roll actual separa lógica de dibujo: las operaciones (mover, estirar, cuantizar, repetir, escalas, carril de
velocidad) están en funciones puras de Kotlin con pruebas (`MusicModelsTest`), y los datos son los `NoteState` que
devuelve el motor. Esa parte se reutiliza entera con Compose Multiplatform. Lo que hay que rehacer es la entrada:
ratón (clic para crear, arrastrar bordes, selección por rectángulo, Ctrl+rueda para zoom) y teclado.

## 6. Alojar plugins VST3 de terceros

Es lo que convierte un programa en un DAW de escritorio de verdad, y es la parte más cara.

- **Licencia**: el SDK de VST3 pasó a licencia MIT en 2025, lo que permite alojar plugins en un producto cerrado.
  *A confirmar en la fuente.*
- **Trabajo técnico**: explorar carpetas de plugins, cargarlos en un proceso aparte (un plugin que falla no debe
  tumbar el programa), pasarles audio y MIDI, automatización, guardar su estado dentro del proyecto, abrir su ventana
  y **compensar su latencia**. El motor ya compensa latencia en pistas, pero la prohíbe en buses
  (PLUGIN_CONTRACT.md §2): habría que resolver eso primero.
- **Compatibilidad con el teléfono**: un proyecto con un VST3 no puede sonar igual en el móvil. La pieza de «plugin
  ausente» ya existe (el proyecto se abre y avisa), y la salida natural es congelar la pista a audio antes de volver
  al teléfono. Congelar pista está investigado y no implementado (RESEARCH_0.5.md).

Recomendación: el alojamiento de VST3 **no** entra en una primera versión de Studio. Primero un Studio que abre,
edita y exporta lo mismo que el móvil, con pantalla grande y ratón.

## 7. Sincronización con el móvil

Ver CAUCE_LINK.md. Studio y el compañero de Windows comparten carpeta de proyectos y motor; Studio no añade
requisitos nuevos salvo uno: si usa plugins que el móvil no tiene, debe decirlo antes de enviar.

## 8. Orden propuesto

1. CI en Windows para `cauce` y las pruebas del motor (convierte en hecho lo que hoy es inferencia).
2. `cauce play`: reproducir un proyecto por WASAPI con miniaudio. Demuestra el motor en tiempo real en Windows.
3. Prueba de rendimiento de Compose Multiplatform con la línea de tiempo.
4. Ventana mínima: abrir, reproducir, mezclador, exportar.
5. Edición (Piano Roll, batería) con ratón y teclado.
6. Mucho después: VST3.

Advertencia como asesor: Studio es un segundo producto. Con una sola persona y el móvil aún sin validar en un
teléfono desde la 0.3.0, mi recomendación es no pasar del paso 2 hasta que la app móvil esté probada y estable.
