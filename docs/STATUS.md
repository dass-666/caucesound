# Estado del proyecto

**Nada está validado físicamente**: este entorno no tiene teléfono ni emulador Android (sin KVM). «Probado» significa
pruebas automáticas en Linux x86-64 (y ARM64 emulado donde se indica). «Render JVM» significa que la pantalla se
dibujó de verdad con Robolectric (gráficos nativos) y se revisó la imagen; **no** es una captura de un teléfono.

# Misión 0.5 «Integración» (2026-11-12, rama `claude/project-thread-hxmmji`)

Categorías: **Implementado** (código en la rama), **Probado** (prueba automática), **Compilado** (entra en el APK
arm64), **Render JVM** (pantalla dibujada con Robolectric y revisada), **Físico** (en un teléfono: hoy, nada).

| Entrega | Implementado | Probado | Compilado | Render JVM | Físico |
|---|---|---|---|---|---|
| Trabajos offline (`JobRunner`): cola, progreso, cancelar, cancelación al cambiar de proyecto | Sí | `jobs_*` (3) | Sí | — | Pendiente |
| VocalShift offline no destructivo (submódulo `10e679c`), presets, A/B, descartar, deshacer | Sí | `vocalshift_*` (10): latencia a la muestra, A/B igualado (A −18,93 dB, B −18,92 dB), silencio, niveles bajos, extremos, mono y estéreo, recorte, cancelar, otro proyecto, temporales, original ausente, guardar y reabrir | Sí | `19`-`22` | Pendiente (54-59) |
| Solo AI 2.0: análisis de beat, corrección por el usuario, 4 alternativas A-D, variación, sustituir solo su pista | Sí | `solo_ai_*` (5) | Sí | `15`, `23`, `24` | Pendiente (51-53) |
| Plugin ausente no destruye el proyecto; latencia prohibida en buses | Sí | 2 pruebas | Sí | — | — |
| Pantallas de VocalShift y Solo AI, accesos desde el clip y la pista | Sí | 13 pruebas nuevas en Android | Sí | Sí | Pendiente |
| Sonido 2.0 | **Sin cambio audible** (dos intentos descartados por medida; ver RESEARCH_0.5.md §6) | 1 prueba de guarda del 808 | — | — | — |
| Spatial Echo, AI Choir, congelar pista, PDC en buses | **No** (investigado; ver RESEARCH_0.5.md) | — | — | — | — |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **114/114** (94 anteriores + 20 nuevas) |
| ASan + UBSan | **112/112** prueba por prueba sobre el commit `cb2463d`; tras el último cambio (A/B igualado) se repitieron las 33 pruebas afectadas (`vocalshift`, `fuzz_plugin`, `jobs_`, `missing_plugin`, `project`): 33/33. Las 2 pruebas nuevas están incluidas en esa repetición |
| TSan | **112/112** prueba por prueba sobre el commit `cb2463d`, sin avisos; tras el último cambio se repitieron las pruebas afectadas: ver la línea siguiente |
| TSan, repetición tras el último cambio | `vocalshift`, `fuzz_plugin`, `jobs_`, `missing_plugin`: 15/15. `project` (18 pruebas): 18/18. Total 33/33, sin avisos |
| ARM64 | **Solo compilación** (NDK, arm64-v8a dentro del APK). Esta sesión no tenía QEMU ni compilador cruzado: las pruebas **no** se ejecutaron en ARM64 |
| Android JVM y Robolectric | **59/59** |
| Lint | 0 errores, 37 avisos (no ocultados) |
| APK `caucesound-0.5.0-integration-preview.apk` | versionCode 7, `com.caucesound.app.preview`, certificado `56b4de46…61ab2e` (el de siempre, comprobado con `apksigner`). SHA-256 `b0bdd15a9c040f3548da80f44a36a700b78c87d418271c60580b4505d5b4970b` |

Límites conocidos de la 0.5:

- **A/B a volumen igualado**: hecho por nivel medio (RMS), que es una aproximación a la sonoridad percibida, no una
  medida de sonoridad (LUFS). Mientras está activo, B suena compensada también en la exportación: «Aceptar» lo quita.
  Si sales de la pantalla sin aceptar, sigue activo (se ve en la propia pantalla).
- **No hay escucha previa** de VocalShift: hay que procesar y después comparar.
- El análisis de beat solo se ha validado con un beat **sintético** (92 BPM, La menor: acierta con confianza 0,90 y
  0,76). Con música real puede dar la mitad o el doble del tempo o la tonalidad relativa: por eso el usuario confirma.
- Las variaciones no cambian la estructura de la canción, así que no existe candado de estructura.
- Guitarra modelada de Solo AI: desactivada.
- Un solo trabajo a la vez; clips de más de 10 minutos se rechazan.

# Hotfix 0.3.1 (2026-11-12, misión 0.4, etapa A, rama `claude/project-thread-hxmmji`)

Fallo observado por Diego en un Samsung con la 0.3.0: tras salir de un proyecto y abrir otro, Play deja de funcionar
(también al volver al primero) hasta reiniciar la app. Se encontraron **dos defectos independientes** que producen
exactamente ese síntoma, y un tercero que podía dejar el audio mudo de forma permanente:

| # | Defecto | Evidencia | Corrección |
|---|---|---|---|
| 1 | El motor vive todo el proceso y `Session::close()` no reiniciaba el transporte: el proyecto siguiente heredaba la posición del anterior. Tras reproducir A más de sus 8 compases, B (y A al volver) arrancaban en silencio más allá de su contenido. Solo reiniciar la app devolvía la posición a 0 | **Demostrado**: `switch_projects_play_after_long_playback` falla con el código de la 0.3.0 (posición heredada 1 440 000, RMS 0,0000 en B y en A) y pasa con la corrección (RMS 0,199) | `Engine::resetTransport()` en cerrar/abrir/crear: parado, posición 0 (visible al instante), cuenta previa cancelada, bucle desactivado hasta que el proyecto aplique el suyo y MIDI en vivo pendiente descartado |
| 2 | Cada Play creaba una petición de foco de audio con un oyente nuevo. Android identifica al dueño del foco por el oyente: la petición nueva le quitaba el foco (`AUDIOFOCUS_LOSS`) a la anterior **de la misma app**, cuyo oyente paraba el transporte. Desde el segundo Play, el transporte se paraba justo después de arrancar | **Inferencia fuerte, no reproducida en teléfono**: leído en el código de Android (AOSP `MediaFocusControl.requestAudioFocus` → `propagateFocusLossFromGain_syncAf`; `FocusRequester.frameworkHandleFocusLoss` despacha la pérdida aunque sea la misma app; `AudioManager.getIdForAudioFocusListener` usa el oyente como id). Robolectric no simula esa pila | Una sola petición y un solo oyente durante toda la app (`AudioGuard`); pedir el foco con él ya concedido no genera pérdida. Se abandona al cerrar el proyecto. `AudioGuardTest`: 100 Play seguidos usan el mismo objeto y no interrumpen |
| 3 | `AudioIO::onErrorAfterClose` solo reabría el stream con `ErrorDisconnected`; con cualquier otro error Oboe lo cerraba, `running` seguía en `true` y `start()` no hacía nada: audio mudo hasta reiniciar | Lectura de código (no observado) | Reapertura con cualquier error; `audioEnsure()` antes de cada Play reabre el stream si no está en estado `Started`. `statusJson` añade `processedFrames`, `outState`, `lastError`, `ensureRestarts` |

Además: con el bucle activo y el cabezal por detrás de la región (seek, bucle acortado), el transporte reproducía
silencio indefinidamente; ahora vuelve al inicio del bucle (`loop_region_behind_playhead_wraps_instead_of_playing_silence`,
falla con el código anterior).

**Cuál de los dos primeros causó lo que viste**: no se puede saber sin el teléfono; ambos producen el mismo síntoma y
ambos están corregidos. Si el fallo persiste en la 0.3.1, Ajustes → Estado del audio muestra `outState`,
`processedFrames` (si no avanza, el callback de Oboe no se ejecuta) y `lastError`.

Pruebas nuevas (motor, `core/tests/TestSwitch.cpp`):
- `switch_projects_play_after_long_playback`: A → Play 30 s → cerrar sin Stop → B → Play → cerrar → A → Play; exige
  posición 0 al abrir y RMS > 0,01 en B y en A.
- `switch_120_sessions_with_audio_thread`: 120 ciclos (216 aperturas) con `process()` en otro hilo como el callback de
  Oboe, sobre las 5 plantillas, un proyecto con delay + compresor + bus de reverb con envío, uno con bucle y uno en el
  que se graba: cerrar con Play en marcha, Stop antes de salir, abrir/cerrar rápido el mismo proyecto 3 veces, stream
  parado y reanudado, seek detrás del bucle, cambio de tempo guardado, nota en vivo sin Note Off al salir. En cada
  apertura: transporte parado en 0, ninguna voz sonando, RMS > 0,01 al pulsar Play, posición que avanza; tras cerrar:
  silencio y 0 instrumentos vivos (ningún grafo antiguo referenciado). Al final, las 11 tomas grabadas siguen en el
  proyecto. Resultado: RMS mínimo al reproducir 0,147, 0 fallos.
- Con la corrección del motor deshecha, la prueba de 120 ciclos falla (posición heredada en cada apertura).

Pendiente de validación física (no hay teléfono aquí): pruebas 30-37 de TESTING_ON_DEVICE.md.

# Pulido 0.3.0 (2026-11-12, misión «Ultimate Polish», misma rama)

Auditoría, rediseño visual y refinamiento sobre la fase 1.2. Categorías usadas abajo: **Implementado** (código en la
rama), **Probado** (prueba automática que lo cubre), **Render JVM** (imagen dibujada con Robolectric y revisada),
**Físico** (probado en un teléfono: hoy, nada).

| Conjunto | Resultado |
|---|---|
| Motor `cs_core_tests` x86-64 Release | **80/80** (78 anteriores + 2 nuevas) |
| ASan + UBSan / TSan | **80/80** / **80/80** |
| ARM64 emulado (QEMU, Linux) | **80/80** (portabilidad, no rendimiento) |
| JVM de la app (`testDebugUnitTest`) | **38/38**: 5 de modelos, 10 del modelo musical, 23 de interfaz |
| `lintDebug` | 0 errores |
| APK `0.3.0-polish-preview`, versionCode 3 | Compila. SHA-256 `9ebfc8dcd590a88acb8e232351c5100c014b9938da920d194a5bfbe33181b6ed`. Certificado `56b4de46…61ab2e`, el mismo que la Preview 0.2.0 (comprobado con `apksigner`): se instala encima. Actualización real en teléfono: pendiente |


## Misión 0.4: sonido, variaciones, Solo AI y botón «Nueva canción» (2026-11-12)

| Entrega | Evidencia | Validación física |
|---|---|---|
| 0.4.0 Sound (commits f472ce1, 544433f): plantillas de 16 compases, 4 kits y 10 presets de síntesis propia, variaciones con semilla, candados, redobles | Motor: TestComposer, 8 pruebas (determinismo, tonalidad, energía, deshacer, candados, picos, estéreo). Render antes/después en `muestras-0.4/` (mono → estéreo −21 a −26 dB lado/centro, 0 recortes, CPU de render ×2 ≈ 2 % en x86) | Pendiente (pruebas 40-44 y 48) |
| Botón «Nueva canción» | Se quitó el margen duplicado de la barra de navegación. Robolectric no simula esa barra, así que su prueba también pasa con el código antiguo: el arreglo se basa en la lectura del código | Pendiente (prueba 47) |
| 0.4.1 Solo AI (commit 26262f0): submódulo fijado, comando `soloMelody`, sección en el panel ✨ | TestSolo, 3 pruebas: tonalidad, reproducibilidad, deshacer, `key_required`, catálogo. 0,3 ms por melodía en x86. Compila para arm64 con el NDK | Pendiente (pruebas 45-46) |
| CI | AudioGuardTest fallaba en la CI (Robolectric SDK 36 exige Java 21): fijado SDK 35 | Pendiente de ver la CI en verde |

Totales en este entorno: motor 94/94 (Release), Android JVM/Robolectric 46/46, lint 0 errores.
TSan (commit del hotfix): 83/83 sin avisos.
APK `caucesound-0.4.1-soloai-preview.apk`: versionCode 6, certificado `56b4de46…61ab2e` (el mismo de siempre).

## Errores reales encontrados y corregidos

| Error | Gravedad | Corrección | Prueba |
|---|---|---|---|
| 25 WAV residuales (137 MB) en la raíz del repo: los escribió el fuzzing con rutas relativas y se subieron con `git add -A` | Media (repo inflado; ningún efecto en la app) | Borrados, `.gitignore`, y la API ahora rechaza rutas no absolutas | `fuzz_command_api…` |
| Perillas y faders: tras deshacer o cargar un preset, la perilla seguía mostrando su último valor local y el siguiente gesto partía de ese valor antiguo | Alta (lo que se ve ≠ lo que suena) | `DragValue`: el valor del motor manda en cuanto cambia | `knob_follows_the_engine_value_after_a_drag_and_an_undo` (JVM) |
| Fader, rejilla de batería, piano roll y línea de tiempo dibujados en píxeles en vez de dp: tamaños distintos según la densidad de pantalla | Media | Todo el dibujo en dp | Render JVM a xhdpi y xxhdpi |
| En teléfonos de 360 dp de ancho la posición del transporte se partía en cinco líneas | Media | Botones algo más pequeños (≥ 40 dp) y texto en una línea | `studio_fits_a_small_phone` + render |
| En horizontal, el fader del mezclador quedaba aplastado y el botón REC cortado | Alta en horizontal | Tira ancha con las perillas junto al fader | Render `16_mixer_land` |
| En horizontal, el piano roll solo mostraba 4 filas (dos barras de herramientas) | Media | Una sola fila de herramientas y carril de intensidad más bajo | Render `17_piano_land` |
| Importar `.caucesound`: un `project.json` o un índice ZIP enormes se leían enteros en memoria | Media (cierre por memoria con un archivo malicioso) | Límites de 64 MB y 16 MB antes de extraer | `project_archive_rejects_malicious…` (14 casos) |
| Parar durante una cuenta previa habría mostrado «no se recibió audio del micrófono» | Baja (función nueva) | Se trata como cancelación limpia | `count_in_clicks_then_records…` |

## Cambios por área

| Área | Implementado | Probado | Render JVM |
|---|---|---|---|
| Sistema de diseño | Paleta carbón/violeta/azul/cian, espaciados, radios, zonas táctiles ≥ 44 dp, `Knob`, `VerticalFader`, `LevelMeter`, `CauceButton`, `ToolButton`, `PanelHeader`, `EmptyState`, `CauceDialog` (ver DESIGN_SYSTEM.md) | Gestos de perilla (arrastre, doble toque, valor externo) | Todas las pantallas |
| Inicio | Crear canción en un toque (nombre automático), plantillas, «Continuar», estado vacío | `home_*` (2) | `01_home` |
| Arreglo | Cabeceras con icono por tipo y acceso directo al editor, menú con pulsación larga, clips con marco y selección, pista de 80 dp | `studio_arrangement_*` | `08`, `09`, `12` |
| Batería | Escucha del kit al cambiarlo, variaciones (dinámica, remate, hats ×2, hats ÷2) deterministas y deshacibles, minimapa de pasos, restablecer carril | `drumVariationsAreDeterministicAndKeepTheGrid` | `02`, `10`, `13` |
| Piano roll | Carril de intensidad por nota, resaltado de escala (mayor, menor, pentatónica, armónica), «Repetir ×2», «+1 compás», notas con borde y relleno según fuerza | `pianoRollRepeatAndVelocityLaneHelpers`, `scalesHighlightTheRightKeys` | `03`, `15`, `17` |
| Synth (motor) | Caída de tono (808), fuerza → filtro, presets reafinados con categoría. Parámetros nuevos al final y a 0 por defecto: los proyectos guardados suenan idénticos | `synth_pitch_envelope_and_velocity_filter…` (bit a bit; 0,00 cents tras la caída) | — |
| Synth (interfaz) | Categorías, favoritos (por dispositivo), restablecer preset, perillas nuevas | `synth_panel_*` (2) | `04`, `18` |
| Mezclador / FX | Tiras con el nuevo fader y medidor, formato ancho en horizontal | `mixer_*` (3), `effects_panel_*` | `05`, `05b`, `07`, `14`, `16` |
| Cuenta previa | 0, 1 o 2 compases antes de grabar: suenan clics, el transporte no avanza y la toma empieza en la posición exacta | `count_in_clicks_then_records_from_the_exact_start_position` (0 muestras erróneas) | — |
| Tap tempo | Botón «Tocar» en el diálogo de tempo (mediana de los últimos 8 intervalos) | `tapTempoUsesTheMedianAndResetsAfterAPause`, `tempo_form_*` | `11_tempo_form` |

## Límites de esta entrega

- **Nada validado físicamente.** Latencia de la cuenta previa, tacto de las perillas, tamaño real de los botones y
  rendimiento solo se sabrán en el teléfono (pruebas 22-28 de TESTING_ON_DEVICE.md).
- La cuenta previa no compensa la latencia de salida del teléfono: los clics se oyen con ese retraso, igual que el
  metrónomo. La compensación de grabación existente (Ajustes → latencia) sigue aplicándose a la toma.
- «Repetir ×2» son dos pasos de deshacer (alargar el clip y copiar las notas).
- Favoritos y cuenta previa son preferencias del dispositivo: no viajan en el `.caucesound`.
- Los diálogos con campos de texto no se pueden renderizar en Robolectric dentro de su ventana (no llega a reposo);
  se prueba su contenido fuera de la ventana. No verificado si ocurre algo parecido en un teléfono (antes del cambio
  ya había campos de texto en diálogos, así que no es una regresión conocida).
- No implementado (propuesto): congelar pista, notas fantasma, duración de nota por defecto configurable,
  indicador de carga DSP permanente en el transporte (hoy está en Ajustes de audio, con datos medidos).
- VocalShift sigue sin integrar: la PDC no compensa latencia dentro de buses.

# Fase 1.2 / 1.3 (2026-11-12, rama `claude/project-thread-w0xy20`)

## Resumen de evidencias

| Conjunto | Resultado |
|---|---|
| Pruebas del motor `cs_core_tests` (x86-64 Release) | **78/78** (28 de la fase 1 + 50 nuevas) |
| Mismas pruebas con ASan + UBSan / con TSan | **78/78** / **78/78** |
| Mismas pruebas en ARM64 (`aarch64-linux-gnu-g++`, QEMU; Linux, no Android) | **78/78**: portabilidad y resultados numéricos, no rendimiento |
| Pruebas JVM de la app (`testDebugUnitTest`) | **24/24**: 5 de modelos (fase 1), 6 del modelo musical, 13 de interfaz con Robolectric |
| `lintDebug` | 0 errores (avisos: versiones nuevas de dependencias, plurales, icono monocromo) |
| APK `0.2.0-fase1.2-preview` (`assemblePreview`) | Compila, firmado con clave de prueba persistente, `com.caucesound.app.preview` |
| Bench x86-64 (Xeon 2,1 GHz), proyecto trap + pad 8 voces + 5 efectos | p99 entre 73 y 176 µs por bloque de 4 ms = **1,8-4,4 %** de un núcleo (la máquina es compartida y el tiempo varía; ver el recuento de instrucciones abajo). **No representa un teléfono**: estimación sin medir 3-6 veces más en gama media |
| Proyecto de 32 pistas (6 baterías, 16 synths con acordes, 10 de audio, 64 efectos) | p99 por bloque de 5,3 ms: 31-34 % antes de optimizar, 17-26 % después. Instrucciones (callgrind, determinista): synth -35 %. **En un teléfono probablemente no cabe en tiempo real**: ver «Rendimiento con proyectos grandes» |

## Continuación de la noche (08:00-11:00 UTC)

| Trabajo | Resultado | Prueba |
|---|---|---|
| Carga y concurrencia | 600-4000 ediciones de efectos al azar (añadir, quitar, mover, bypass, parámetros, deshacer) con el audio sonando: salida finita, modelo coherente, reabre idéntico; TSan limpio | `stress_fx_chain_edits_while_playing_are_thread_safe` |
| Sesión larga | 30 vueltas de bucle (3,4 min): 0 reservas, energía por vuelta estable (0,12 dB), posición sin deriva, batería idéntica a la muestra en la vuelta 3 y la 30, 0 voces al parar | `stress_long_loop_session_is_deterministic_and_does_not_drift` |
| Proyecto grande | 32 pistas: guarda en ~5 ms, abre en ~150-190 ms (genera los kits), la pista 33 se rechaza, 0 reservas | `stress_large_project_32_tracks_saves_reopens_and_renders` |
| Desnormales | El callback y la exportación ponen los desnormales a cero (FTZ/DAZ en x86, FZ en ARM64) y lo restauran al salir. Sin ello, las colas en silencio costaban ~4,5 veces más en x86 | `stress_decaying_tails_never_produce_denormals` (sonda determinista, también en ARM64) |
| Revisión de pruebas | `editing_patterns_while_playing_is_thread_safe` terminaba en `CHECK (true)`: ahora comprueba salida y modelo. Las de «igual con cualquier bloque» comprueban que la referencia no es silencio. `pdc_export_matches_live…` no comparaba con el vivo: ahora sí (diferencia 0). Nueva `pdc_after_seek…` fija el comportamiento tras un seek | — |
| Deshacer por gesto | Un giro de perilla (synth, batería, efectos) = una entrada; preset y kit también se deshacen. **Error real corregido**: deshacer otra edición dejaba la instancia sonando con un valor y guardaba otro (diferencia 0,87 entre exportar antes y después de reabrir; 0 con la corrección). El mezclador sigue fuera del deshacer, como en la fase 1 | `param_gestures_undo_as_one_step_and_live_instances_follow` |
| Exportar/importar proyecto | `.caucesound` = ZIP sin compresión con project.json + WAV usados; abre con cualquier descompresor (comprobado con `unzip -t` y Python). Importar rechaza rutas peligrosas, comprimidos, cifrados, CRC malos, truncados y proyectos sin su audio, sin dejar restos; id nuevo. En la app: menú ⋮ → Exportar; botón Importar en el inicio (selector del sistema, sin permisos) | `project_archive_*` (2), `home_import_and_export_actions_are_wired` (JVM) |
| Calidad de presets | **Error real corregido**: Lead y Pad sonaban 6 y 9 cents altos (solo subía el oscilador 2); ahora el detune se reparte. Bajos reequilibrados (diferencia 8,6 → 3,5 dB). Todos: afinación ≤ 0,1 cents (Synth Bass +2,7, estimación sobre onda filtrada), DC despreciable, colas que terminan, sin clic al soltar | `synth_presets_are_in_tune_balanced_and_end_cleanly` |
| Aliasing | Seno -88/-92 dB; triángulo -44/-53; sierra -38/-41; cuadrada -40/-43 (Do 6 / Do 7, sin filtro). Con el filtro del preset Lead: -65/-70 dB. Sierra y cuadrada agudas sin filtro: mejorable con sobremuestreo x2 (pendiente) | `synth_oscillators_have_low_aliasing_at_high_notes` |
| Kits | 4 kits: picos de -3,4 (bombo) a -11 dB (hats), bombo y caja a 2,1 dB, colas de 119 a 1500 ms, sin DC | `drum_kits_are_balanced_short_and_clean` |
| Fuzzing | 4000 comandos JSON al azar (tipos equivocados, ids reales y falsos, extremos, texto que no es JSON) con el audio sonando: 0 fallos, toda respuesta válida, proyecto final válido y reabre igual. 400 project.json dañados al azar: cada uno se rechaza con error o se abre válido y suena. **Error real corregido**: un argumento de tipo equivocado se sustituía en silencio por el valor por defecto (`{"bpm":"rápido"}` ponía el tempo a 120) y números enormes se convertían a entero con comportamiento indefinido; ahora se rechazan | `fuzz_command_api_never_crashes_and_leaves_a_valid_project`, `fuzz_damaged_project_json_is_rejected_or_opened_safely` |
| Medidor de carga DSP | El motor mide cada callback (tiempo / duración del bloque): media de ~1 s, pico desde la última lectura y bloques fuera de tiempo. Se ve en Ajustes → estado del audio. Sirve para medir el límite real en el Samsung (prueba 16) | `dsp_load_meter_reports_sane_values` (0 reservas) |
| Rendimiento | Seno polinómico en el oscilador (error 5,6e-8), reverb sin división por muestra | `fast_sine_matches_std_sin` + todas las de salida idéntica |
| Buses de efectos | Pista de tipo bus (máx. 4) con envíos post-fader de 0 a 1 desde cualquier pista. Enrutado exacto a la muestra (0,313391 medido = esperado), mute de la pista corta su envío, el solo no silencia el bus, exportar = reproducir e incluye la cola de la reverb del bus. Se guarda, se deshace al borrar el bus y se valida al abrir. 8 synths con una reverb compartida: ~45-50 % menos tiempo por bloque que con una reverb cada uno (Xeon). En la app: Mezclador → «+ Bus de reverb» y perilla Send en cada pista. **Límite**: la PDC no compensa la latencia de efectos dentro del bus (hoy ningún efecto propio tiene latencia) | `bus_*` (2), `shared_reverb_bus_costs_less_than_one_reverb_per_track`, `mixer_*bus*` (JVM, 2; incluye girar la perilla Send) |
| Revisión de pruebas de interfaz | Las pruebas de perillas apuntaban al nodo fusionado de la tira (clicable), así que el toque caía en el centro de la tira, sobre el fader, y no en la perilla. Ahora se usa el árbol sin fusionar: arrastre y doble toque en Pan y Send comprobados. No era un fallo de la app | `mixer_panel_toggles_mute`, `mixer_shows_engine_sends_to_the_bus` |
| Rutas en la API | **Error real corregido** (lo encontró el fuzzing): una ruta relativa (p. ej. un id usado como `path`) escribía un WAV en el directorio actual del proceso. Ahora folder/path/out/archive/dest/root deben ser absolutas | final de `fuzz_command_api…` |

### Rendimiento con proyectos grandes (inferencia, sin medir en teléfono)

El proyecto de 32 pistas usa entre el 17 % y el 26 % de un núcleo Xeon tras optimizar. Si un núcleo grande de un
Samsung de gama media rinde de 3 a 6 veces menos en este código, quedaría entre el 50 % y más del 100 %. Es decir, al
límite o fuera de tiempo real. El perfil muestra dónde está el coste: el synth (~30 %, voces con dos osciladores y
filtro por muestra) y la reverb (~20 %, una instancia por pista). Lo más rentable, por orden:
1. **Envíos a un bus de reverb compartido** en lugar de una reverb por pista (lo normal en mezcla; quita la mayor
   parte del 20 %). **Hecho** esta noche: ver «Buses de efectos» arriba.
2. **Congelar pista** (renderizar a audio una pista de synth terminada).
3. Sobremuestreo o SIMD solo si las mediciones en el teléfono lo piden.
La prueba 16 de TESTING_ON_DEVICE.md (carga) dirá dónde está el límite real.

## Por bloque de la misión

| Bloque | Implementado | Probado automáticamente (prueba) | Render JVM | Físico |
|---|---|---|---|---|
| A. Auditoría y estabilidad | Sin ramas/PR paralelas; CI en verde en `main`. Corregidos: arnés de pruebas (`operator new` nothrow, ASan), `cs.gain` aceptaba NaN y lo guardaba como `null` | ASan/TSan limpios | — | No |
| B. Diseño visual | Tema oscuro (carbón, violeta, azul eléctrico, cian), inicio con logo, continuar, plantillas, recientes con miniatura, renombrar/duplicar/borrar con confirmación; estudio con transporte grande, navegación inferior animada, modo horizontal compacto | Interacciones básicas en `UiScreensTest` | 10 capturas (`docs` → ver «Capturas») | No |
| C. Cauce Drums | 8 carriles, 16/32 pasos, velocidad, swing, patrones (nuevo, duplicar, copiar/pegar, limpiar), encadenado en el arreglo, pads multitáctiles, 4 kits sintetizados (Trap, Reggaetón, Hip-hop, Electrónica), volumen/pan/tono/decay/mute/solo por carril, choke de hats | `pattern_expands_…`, `drum_kits_have_distinct_sounds…`, `drum_lane_mute_solo_volume_and_hat_choke`, `editing_patterns_while_playing_is_thread_safe` | Sí (vertical) | No |
| D. Núcleo MIDI | Note On/Off, velocidad, canal, posición y duración en ticks (PPQ 960), clips, persistencia, cuantización con intensidad; eventos en frames absolutos con división de bloque → exacto a la muestra con cualquier tamaño de bloque; bucle, pausa, seek, exportación; sin notas colgadas | `midi_ticks_frames_quantize_and_swing`, `synth_note_is_sample_accurate_for_any_block_size`, `note_off_stop_seek_and_edits_never_leave_stuck_notes`, `loop_wraps_at_the_exact_frame…`, `tempo_change_moves_midi_events_but_not_audio` | — | No |
| E. Piano roll | Dibujar, mover, redimensionar, borrar (doble toque), vista previa al tocar, velocidad, cuantizar, rejilla 1/4–1/32, zoom y desplazamiento con dos dedos, selección por rectángulo, seleccionar todo, duplicar, transponer | `MusicModelsTest.pianoRollEditOperations` (operaciones), API `setClipNotes`/`quantizeClip` | Sí | No: los gestos no se han probado con dedos reales |
| F. Cauce Synth | Seno/triángulo/sierra/cuadrada con PolyBLEP, 2 osciladores, filtro SVF con envolvente, ADSR, octava, glide/mono, 1-16 voces con robo de voz, saturación suave; 7 presets (Sub Bass, 808 Bass, Lead, Soft Keys, Pad, Pluck, Synth Bass); teclado de 2 octavas multitáctil | `synth_voice_limit_levels_and_extreme_parameters_are_safe`, `mono_synth_glides…`, `live_midi_plays_while_transport_is_stopped` | Sí | No |
| G. Flujo completo | 140 BPM → beat → bucle → synth → melodía → grabar voz sobre la instrumental → mezcla → guardar → reabrir → exportar | `full_flow_beat_melody_vocal_mix_save_reopen_export`: exportación = reproducción en vivo (diferencia 0), voz grabada con la instrumental sonando | — | No |
| Plantillas | Trap 140, Reggaetón 95 (dembow), Hip-hop 90 (swing), Pop 105, Electrónica 124: batería + 2-3 synths con progresión de 4 acordes propia + pista de voz armada. Vacío: proyecto sin pistas | `templates_create_real_playable_content` (picos 0,55-0,62: la prueba detectó saturación antes de ajustar volúmenes) | Sí (inicio) | No |
| Cauce FX (opcional) | Ganancia, filtro LP/HP, EQ 3 bandas, saturación, compresor (con medidor), delay (ping-pong), reverb; hasta 8 por pista, bypass, reordenar, deshacer; colas incluidas en la exportación | 8 pruebas `fx_*` con mediciones (LP −53 dB, EQ +11,8 dB, compresor −12,9 dB, eco en la muestra exacta, reverb −128 dB al final de la cola, 0 reservas) | Sí | No |
| Compatibilidad | Formato v2; los proyectos v1 se abren sin cambios, se migran al guardar y se conserva `project.v1.json` | `v1_project_opens_unchanged_and_is_migrated_with_backup`, `future_format_and_broken_music_data_are_rejected` | — | No |
| Rendimiento | Sin reservas, bloqueos, disco ni red en el callback con batería, synths, efectos, bucle y MIDI en vivo; NaN neutralizados; límite de voces | `engine_with_instruments_loop_and_live_input_does_not_allocate`, `fx_process_does_not_allocate` | — | **CPU real del Samsung sin medir** |

## Capturas (render JVM, no teléfono)

`android/app/build/outputs/roborazzi/` tras `./gradlew testDebugUnitTest -Pscreenshots` (copias en la carpeta del
proyecto `capturas-jvm-fase1.2`). Se generan con datos JSON producidos por el propio motor (`ui_fixtures_from_engine`).
Durante la revisión de estas imágenes se encontraron y corrigieron: la rejilla del piano roll pintando sobre la regla,
el piano roll abriendo fuera de las notas, tiras del mezclador de distinta altura, metrónomo y bucle fuera de la
pantalla en vertical y solo 1,5 pistas visibles en horizontal.

## Limitaciones conocidas (fase 1.2)

- **Validación física pendiente en todo**: latencia de pads y teclado, gestos multitáctiles, CPU, temperatura.
- La pantalla de ajustes, diálogos y selector de archivos no tienen captura (dependen del sistema o del motor).
- PDC implementada (la pista latente lee su contenido adelantado; probado a la muestra en vivo, en bucle y en
  exportación con un efecto de prueba de 7200 muestras). Límites: tras arrancar o hacer seek, los primeros L frames
  de esa pista son el retardo del efecto, y la entrada en vivo no se puede compensar. Ningún efecto propio tiene
  latencia, así que hoy no cambia nada audible; validar con VocalShift real.
- Sin automatización, pitch bend, CC, aftertouch, MIDI externo ni importación/exportación de archivos MIDI
  (el modelo admite añadirlos: ver ARCHITECTURE.md §6).
- Deshacer agrupado por gesto en perillas de synth, batería y efectos. El mezclador (volumen, pan, mute, solo de
  pista) sigue fuera del deshacer, como en la fase 1.
- Los proyectos de la app de la fase 1 **no aparecen** en la app Preview (son apps distintas). La app de la fase 1
  no tiene «Exportar proyecto» (se añadió ahora y no se puede actualizar): se rescatan por pistas (Solo + Exportar WAV): ver TESTING_ON_DEVICE.md.
- Sierra y cuadrada sin filtro en notas muy agudas: aliasing de -38/-40 dB (medido); con los filtros de los presets,
  por debajo de -65 dB.
- Lista de carriles con nombres largos se recorta en vertical («Kick 8…»).

## Fase 1 (histórico, 2026-11-12, commit dbdd93b)

**Nada está validado físicamente**: este entorno no tiene teléfono ni emulador Android (sin KVM). Todas las
afirmaciones de «probado» se refieren a pruebas automáticas en Linux x86-64 y ARM64 emulado.

| # | Requisito de la fase 1 | Implementado | Probado automáticamente | Validado físicamente |
|---|---|---|---|---|
| 1 | Pantalla de proyectos | Sí (`ProjectsScreen.kt`) | Parcial: el listado (`command_api_drives_a_full_session`); la pantalla no | No |
| 2 | Crear y abrir proyectos | Sí | Sí: `project_create_edit_save_reopen_roundtrip` (render bit a bit idéntico tras reabrir) | No |
| 3 | Timeline multipista con formas de onda | Sí (`Timeline.kt`) | Parcial: picos (`waveform_peaks_are_correct`), geometría píxel↔frame (JVM). El dibujo no | No |
| 4 | Importar audio | Sí: WAV directo (8/16/24/32 bits, float), MP3/AAC/FLAC/OGG vía MediaCodec del sistema | WAV y remuestreo sí (`wav_*`, `resampler_*`, `import_resamples_to_project_rate`). MediaCodec **no** (requiere Android) | No |
| 5 | Grabar del micrófono con permisos | Sí: permiso en tiempo de ejecución, Oboe dúplex, hilo de disco | Motor y archivo sí, con entrada simulada (`recording_places_audio_at_the_right_position`: 144 000 frames exactos). Oboe/micrófono no | No |
| 6 | Reproducción sincronizada de ≥4 pistas | Sí (hasta 32) | Sí: `engine_four_tracks_sample_accurate_sync` (alineación exacta a la muestra con 4 bloques distintos) | No |
| 7 | Volumen, mute, solo, pan | Sí | Sí: `engine_mute_solo_and_balance_pan` (niveles exactos, rampa sin clics) | No |
| 8 | Tempo y metrónomo | Sí (20–400 BPM, 1–16 tiempos, acento en el 1) | Sí: `engine_metronome_is_sample_accurate_without_drift` (120/133/87,5 BPM, 20 s, sin deriva) | No |
| 9 | Edición básica de clips | Sí: mover (también entre pistas), recortar ambos bordes, dividir, duplicar, borrar, fundidos, ganancia, deshacer/rehacer (100 pasos), ajuste a tiempos | Sí: `clip_editing_and_undo_redo`, `engine_clip_gain_and_fades`. Los gestos táctiles no | No |
| 10 | Guardar, reabrir y exportar WAV | Sí: guardado atómico + copia `.bak`, autoguardado 15 s, exportación 16 (dither)/24/32f | Sí: `project_survives_crash_during_save`, `export_matches_live_playback_and_formats_are_valid` (exportación = reproducción, 0 muestras distintas) | No |

## Calidad pedida en la sección 6

| Aspecto | Qué hay | Evidencia | Falta |
|---|---|---|---|
| Sincronización entre pistas | Render por muestra, bloques arbitrarios | `engine_four_tracks_sample_accurate_sync`, `engine_output_is_identical_for_any_block_size` | Confirmar en el teléfono que el clic y las pistas suenan juntos |
| Estabilidad del motor | 0 reservas en el callback, NaN/inf neutralizados, intercambio de grafos sin bloqueos | `engine_process_does_not_allocate`, `engine_graph_swap_stress_is_safe`, `engine_input_nan_and_monitoring_are_safe`; ASan+UBSan y TSan limpios | Sesiones largas en el teléfono (xruns) |
| Latencia de grabación y monitoreo | Se muestran las estimaciones de Oboe (`calculateLatencyMillis`) y hay compensación manual | Nada medido | **Medición física** (ver TESTING_ON_DEVICE.md) |
| CPU y memoria | Bench de escritorio | x86-64 de este entorno: 4 pistas p99 4,4 µs por bloque de 4 ms (0,11 %); 32 pistas 1,2 %. **No representa un teléfono** | Perfil en el Samsung; memoria por minuto de audio documentada (23 MB/min estéreo) |
| Recuperación tras interrupciones | Pérdida de foco de audio y «audio becoming noisy» detienen y guardan; salir de la app detiene y guarda | Lógica de sesión probada; los avisos de Android no | Llamada entrante, alarma, asistente de voz en el teléfono |
| Desconexión de auriculares o interfaces | Oboe `ErrorDisconnected` → reapertura del stream; transporte detenido | Solo revisión de código | Probar cable, USB-C, Bluetooth |
| Guardado seguro tras cierre inesperado | tmp + fsync + rename + `.bak`; grabación recuperable | `project_survives_crash_during_save`, `recording_is_recovered_after_unexpected_close` (96 000/96 000 frames) | Forzar cierre real durante grabación en el teléfono |
| Exportaciones sin corrupción | WAV atómico, encabezado correcto, determinista | Exportar dos veces = bytes idénticos; relectura y comparación | Abrir el WAV en otro programa (FL Studio, Audacity) |
| Cableado vs Bluetooth | La app detecta la ruta y avisa en Bluetooth | Solo código | Medir ambos en el teléfono |

## Compilación (este entorno)

- `cs_core_tests`: **28/28** (x86-64 Release), **28/28** (ASan + UBSan), **28/28** (TSan), **28/28** (ARM64 en QEMU).
- Pruebas JVM de la app (`testDebugUnitTest`): **5/5**. Durante el desarrollo detectaron un error real de redondeo
  píxel→frame, ya corregido.
- `assembleDebug` y `assembleRelease`: correctos (NDK r27c, AGP 8.13.2, Kotlin 2.2.21, Oboe 1.10.0).
  Bibliotecas nativas alineadas a 16 KB (`zipalign -c -P 16`: correcto).
- `lintDebug`: 0 errores; avisos de versiones nuevas disponibles y de icono monocromo.
- **No ejecutado**: la app en ningún dispositivo ni emulador.

## Limitaciones conocidas

- El audio de los proyectos se carga entero en memoria (ver ARCHITECTURE.md §4).
- Sin servicio en primer plano: no se graba con la app en segundo plano.
- Monitorización sin efectos (la entrada en vivo no se compensa con PDC).
- Sin bucle (loop), sin cuenta atrás antes de grabar, sin tomas alternativas/comping, sin time-stretch.
- La primera vez que se pulsa grabar sin permiso, Android pide el permiso y hay que volver a pulsar grabar si se
  rechaza; si se concede, la grabación arranca sola.
