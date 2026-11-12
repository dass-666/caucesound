# Estado del proyecto

**Nada está validado físicamente**: este entorno no tiene teléfono ni emulador Android (sin KVM). «Probado» significa
pruebas automáticas en Linux x86-64 (y ARM64 emulado donde se indica). «Render JVM» significa que la pantalla se
dibujó de verdad con Robolectric (gráficos nativos) y se revisó la imagen; **no** es una captura de un teléfono.

# Fase 1.2 / 1.3 (2026-11-12, rama `claude/project-thread-w0xy20`)

## Resumen de evidencias

| Conjunto | Resultado |
|---|---|
| Pruebas del motor `cs_core_tests` (x86-64 Release) | **72/72** (28 de la fase 1 + 44 nuevas) |
| Mismas pruebas con ASan + UBSan / con TSan | **72/72** / **72/72** |
| Mismas pruebas en ARM64 (`aarch64-linux-gnu-g++`, QEMU; Linux, no Android) | **72/72**: portabilidad y resultados numéricos, no rendimiento |
| Pruebas JVM de la app (`testDebugUnitTest`) | **22/22**: 5 de modelos (fase 1), 6 del modelo musical, 11 de interfaz con Robolectric |
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
| Rendimiento | Seno polinómico en el oscilador (error 5,6e-8), reverb sin división por muestra | `fast_sine_matches_std_sin` + todas las de salida idéntica |

### Rendimiento con proyectos grandes (inferencia, sin medir en teléfono)

El proyecto de 32 pistas usa entre el 17 % y el 26 % de un núcleo Xeon tras optimizar. Si un núcleo grande de un
Samsung de gama media rinde de 3 a 6 veces menos en este código, quedaría entre el 50 % y más del 100 %. Es decir, al
límite o fuera de tiempo real. El perfil muestra dónde está el coste: el synth (~30 %, voces con dos osciladores y
filtro por muestra) y la reverb (~20 %, una instancia por pista). Lo más rentable, por orden:
1. **Envíos a un bus de reverb compartido** en lugar de una reverb por pista (lo normal en mezcla; quita la mayor
   parte del 20 %).
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
