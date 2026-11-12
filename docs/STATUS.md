# Estado de la fase 1 (2026-11-12)

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
- Monitorización sin efectos y sin compensar la latencia de las inserciones.
- Sin bucle (loop), sin cuenta atrás antes de grabar, sin tomas alternativas/comping, sin time-stretch.
- La primera vez que se pulsa grabar sin permiso, Android pide el permiso y hay que volver a pulsar grabar si se
  rechaza; si se concede, la grabación arranca sola.
