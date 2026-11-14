# Estado del proyecto

**Nada está validado físicamente**: este entorno no tiene teléfono ni emulador Android (sin KVM). «Probado» significa
pruebas automáticas en Linux x86-64 (y ARM64 emulado donde se indica). «Render JVM» significa que la pantalla se
dibujó de verdad con Robolectric (gráficos nativos) y se revisó la imagen; **no** es una captura de un teléfono.

# Hotfix 0.9.2 (2026-11-14, rama `claude/project-thread-sol38i`, basada en la 0.9.1 / PR #7)

Detalle, causa real y mediciones en HOTFIX_0.9.2.md. Estudio de VocalShift en vivo en VOCALSHIFT_LIVE_STUDY.md.

| Entrega | Implementado | Probado (nube) | Físico |
|---|---|---|---|
| Variar todo / Regenerar beat / botones por parte con cambio garantizado (8 intentos, umbrales 15/25/40 %), versión, informe por parte, errores claros | Sí | `TestVariations.cpp` (8 estilos, audio >300 Hz durante Play, candados, persistencia) y `EndToEndVariationsTest` desde el botón con JNI real | Pendiente |
| Pad y Rehacer Bajo (Pop, Dark) que no hacían nada; acordes Dark | Corregido en el compositor; semillas por defecto = sonido anterior | Mismas pruebas | Pendiente (**oído**) |
| Tarjeta de versión: Generando…, Versión N, cambios, candados, Escuchar | Sí | `Hotfix092UiTest` | Pendiente |
| Navegador de plugins en el Mixer por categorías, controles al agregar, VocalShift (no en vivo) y Solo AI en secciones propias | Sí | `Hotfix092UiTest`, `ChannelRackMixerTest` | Pendiente |
| VocalShift como inserción en vivo | **Prueba de concepto oculta** (no en el catálogo) | Latencia 150 ms compensada a 0 muestras, 0 reservas, ~3 % de un núcleo x86 | No medido en ARM |
| Grabación de un toque y «Grabar sin acompañamiento» | Sí | Motor (`record_without_backing…`) y JVM (master en silencio durante la toma) | Pendiente (micrófono) |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **169/169** (suite completa) |
| ASan + UBSan | Pruebas nuevas y afectadas (variaciones, VocalShift en vivo, grabación, PDC, efectos, Mixer, inserciones, sampler concurrente): **23/23**, sin informes. La suite completa la ejecuta la CI |
| TSan | Variaciones, VocalShift en vivo, grabación y PDC: **13/13**, sin avisos de carrera. `sampler_concurrent…` (prueba de la 0.9, no tocada) falló 1 de 4 veces por el recuento de instancias, sin aviso de TSan; es inestable con carga (también falló una vez en Release con otra suite en paralelo) |
| Android JVM y Robolectric (`testDebugUnitTest` con `CS_HOST_JNI_DIR`) | **123/123**, 0 omitidas (incluye las 3 de extremo a extremo con el JNI real) |
| Lint | 0 errores, 38 avisos |
| CI | CI_RESULT |
| APK `caucesound-0.9.2-hotfix-preview.apk` | versionCode 14, `0.9.2-hotfix-preview`, `com.caucesound.app.preview`, certificado `56b4de46…61ab2e` (el mismo), 37,3 MB, paquetes idénticos a la 0.9.1 (191 archivos), SHA-256 `724fb1ac0be0e465b352be6fe1f5514f55fb0e77911588c0bb26ca2bf6e599b1` |

# Misión 0.9 «Signature Sounds» (2026-11-13, rama `claude/project-thread-y494z5`, PR #6 borrador)

Mismas categorías que en la 0.8. «Render JVM» son las capturas de `capturas-0.9.0/` (90-95).

| Entrega | Implementado | Probado | Compilado | Audio real | Render JVM | Físico |
|---|---|---|---|---|---|---|
| Auditoría del sonido artificial (fase A) | Informe en CAUCE_SAMPLER.md §1 | — | — | — | — | — |
| Cauce Sampler (SFZ, capas de fuerza, round robin, choke, sustain, robo de voces sin clics, int16, Hermite) | Sí, motor propio (sfizz está archivado) | 16 pruebas en `TestSampler.cpp`: sin reservas en el hilo de audio, independencia de bloque, rutas maliciosas, WAV dañados, presupuesto de memoria, concurrencia | Sí | WAV en `muestras-0.9/` | — | Pendiente |
| Paquetes Cauce (manifiesto + SHA-256) e importación segura de ZIP | Sí (`CAUCE_PACK_FORMAT.md`) | Zip-slip, ZIP corrupto, hash erróneo, versión, borrado protegido si se usa | Sí | — | `92` | Pendiente |
| Kits Raw Boom Bap, Dark Trap, Latin Pulse; 808 Lab y Deep Sub; Cauce Electric Bass (CC0); Cauce Grand Piano (CC0, opcional) | Sí | Los 4 ZIP importan y verifican con el motor; afinación del bajo ±0,2 semitonos | En el APK salvo el piano | `_5_kit_*`, `_6_*` | `90`, `93`, `95` | Pendiente (**juicio de oído**) |
| Explorador de sonidos: categorías, estilos, búsqueda sin acentos, favoritos, escuchar, cargar, importar, borrar, aviso de sonidos que faltan | Sí | `SoundBrowserTest` (10) + `sampler_json_api_packs_browser_flow` | Sí | — | `90`-`94` | Pendiente |
| Proyecto v4 solo si se usa un sonido 0.9 (copia `project.v3.json`), Deshacer de cambio de instrumento/kit | Sí | `sampler_project_integration_v4_only_when_used_and_undo` | Sí | — | — | Pendiente |
| Regenerar en Electrónica con más familias de groove (breakbeat, medio tiempo) | Sí | Golpes distintos 30-35 % (antes casi iguales) | Sí | `electronic_3/4_*` | — | Pendiente |
| AutoMix por reglas, reversible (no es IA) | Sí (`AUTOMIX.md`) | `automix_rules_suggest_reversibly`: plantilla sin tocar = sin cambios; acordes +9 dB = propone bajarlos; Revertir restaura | Sí | — | — | Pendiente |
| Humanización de tiempo controlable | **No** | — | — | — | — | — |
| Hip-hop oscuro con más presencia en altavoz de móvil | **Solo vía kit Dark Trap** (+5 dB sobre 150 Hz); la plantilla no se tocó para conservar el balance 0.6 | Métrica en `muestras-0.9/metricas.json` | — | Sí | — | Pendiente |
| Plantillas existentes | Sin cambios: render idéntico muestra a muestra al de la 0.8 en los 8 estilos | `cs_render --compare09` | Sí | — | — | — |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **161/161** |
| ASan + UBSan | **161/161** (suite completa, salida 0, sin informes) |
| TSan | **161/161**, sin avisos de carrera. En la suite completa falló 1 (`daw_export_with_drum_piece…`) porque la suite Release se ejecutaba a la vez y ambas escribían en `/tmp/cs_test_daw_lane`; repetida sola bajo TSan: correcta (diferencia de nulo 0) |
| ARM64 | Solo compilación (arm64-v8a dentro del APK) |
| Android JVM y Robolectric (`testDebugUnitTest`) | **97/97** |
| Lint | 0 errores, 37 avisos |
| CI (GitHub Actions) | `core` y `android` **en verde** en `eede838` (push y pull_request del PR #6) |
| APK `caucesound-0.9.0-signature-sounds-preview.apk` | versionCode 12, `0.9.0-signature-sounds-preview`, `com.caucesound.app.preview`, certificado `56b4de46571e04c4e7ee2add76fbbfb2205f81b4ce91ef913a17efff5f61ab2e` (el mismo). 37,1 MB (0.8: 20,2 MB) por los sonidos. SHA-256 `8bcd70cf321b0c173db0d31a1408651e63cc17f55c7a444c8756c4e29cec55f6` |

Límites conocidos de la 0.9:

1. Nada probado en un teléfono: rendimiento del sampler en ARM (en x86: 10,4 ms de CPU por segundo con 32 voces),
   tiempo del primer arranque (copia 16 MB), memoria con piano + bajo + kit.
2. El piano está recortado a 1,4-3 s por nota para que el paquete pese 42 MB: las notas largas se apagan antes.
3. Sin guitarras ni cuerdas reales (no se encontró licencia clara para acústica; VSCO 2 CE queda para la 1.0).
4. Licencia de los sonidos sintetizados por CAUCESOUND (`LicenseRef-CauceSound-Content`) provisional.
5. `testPreviewUnitTest` no es un conjunto válido (la variante preview no incluye el manifiesto de pruebas de Compose):
   el conjunto de referencia es `testDebugUnitTest`.
6. Los proyectos guardados en v4 no se abren en la 0.8 (los v3 siguen siendo v3).

# Misión 0.8 «Studio Flow» (2026-11-13, rama `claude/project-thread-nhte0l`)

Mismas categorías que en la 0.6 y la 0.7. «Render JVM» son las capturas de `capturas-0.8.0/`.

| Entrega | Implementado | Probado | Compilado | Audio real | Render JVM | Físico |
|---|---|---|---|---|---|---|
| Play entre proyectos (fase 1) | Ya corregido en la 0.3.1; no se reimplementó | Pruebas de `TestSwitch` en verde | Sí | — | — | Diego lo validó en la 0.6.0 |
| Piano Roll: mover hacia atrás. Causa: el desplazamiento se redondeaba con `MusicTime.snapTicks`, que convierte en 0 cualquier valor negativo | Sí: `PianoRollOps.dragMove` / `dragResize`, imán sobre la posición final, opción «Libre», tono al cambiar de tecla | `PianoRollDragTest` (7): 8 direcciones, varias veces hacia atrás, límites 0 y 0-127, grupo sin deformarse, notas fuera de rejilla, duración mínima, notas no seleccionadas intactas | Sí | — | `03`, `15`, `17` | Pendiente (**tacto**) |
| Mixer: faders, medidores, pan, M/S siempre visibles; efectos ocultos por defecto con barra «Ver efectos»; FX de la tira abre su rack; volumen del canal sobre sus efectos; indicador de saturación | Sí | `mixer_task_flow…` y las de `ChannelRackMixerTest` | Sí | — | `05`, `14`, `16`, `47` | Pendiente |
| Grabación: ¿la toma lleva beat? | Diagnóstico: **no por vía digital** | `recording_takes_only_the_microphone_never_the_master_mix`: con plantilla, metrónomo y monitorización, error máximo de la toma 0 | — | — | — | Pendiente: si hay beat en la toma, es fuga acústica (altavoz → micrófono) |
| Modo «Con bocinas»: el beat baja solo en la salida en vivo mientras se graba (0 a −24 dB, rampa ~20 ms) | Sí (`Engine::setRecordDuck`) | `speaker_mode_ducks_only_the_live_output_while_recording`: −12 dB da 0,256 (teórico 0,251); al parar vuelve a 1,06 | Sí | — | No (sin prueba JVM de la sección) | Pendiente |
| Cancelación de eco del teléfono (opcional, preset VoiceCommunication de Oboe) | Sí | **Sin prueba automática** (depende del teléfono) | Sí | — | No | Pendiente. No guarda toma sin procesar |
| VocalShift «Ajustar escuchando»: bucle de la región, recálculo 250 ms después de cada cambio, A/B, el original intacto | Sí | `vocalshift_continuous_listen_replaces_stale_previews…`: 5 cambios seguidos, 4 sustituidos, se aplica el último a los 0,24 s (x86) + prueba de interfaz | Sí | — | `48` | Pendiente (latencia en ARM sin medir) |
| Estilos Boom Bap crudo, Jazz Rap, Hip-hop oscuro (kits 8-10, 5 presets, swing, progresiones y bajo propios) | Sí | `hiphop_styles_are_really_different` + las de calidad de todas las plantillas | Sí | WAV en `muestras-0.8/`, sin recortes | — | Pendiente (**juicio de oído**) |
| Variar (conserva progresión y motivo) frente a Regenerar (otra progresión, groove y partes; respeta bloqueos) | Sí (`mutatePart`, `regenerateBeat`) | `vary_keeps_identity…` (parecido medio 0,62 frente a 0,28; en las 31 partes Variar se parece más al original), `regenerate_keeps_locked_parts…`; un solo paso de deshacer; reproducible por semilla; sobrevive a guardar | Sí | WAV comparativos | `12` | Pendiente |
| Plantillas existentes | Sin cambios: la semilla por defecto reproduce exactamente el patrón de la 0.7 | Pruebas de composición existentes | Sí | — | — | — |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **145/145** |
| ASan + UBSan | **145/145** prueba por prueba, sin informes de ASan ni UBSan (fuzz de comandos con 1500 iteraciones) |
| TSan | **145/145** prueba por prueba, sin avisos de carrera |
| ARM64 | Solo compilación (arm64-v8a dentro del APK) |
| Android JVM y Robolectric | **87/87** |
| Lint | 0 errores, 38 avisos y 9 sugerencias (no ocultados) |
| CI (GitHub Actions) | Trabajos `core` (Release + ASan) y `android` **en verde** en `e62353c` (push y pull_request del PR #4); también en `db43665` y `16ba3c9` |
| APK `caucesound-0.8.0-studio-flow-preview.apk` | versionCode 10, `0.8.0-studio-flow-preview`, `com.caucesound.app.preview`, certificado `56b4de46571e04c4e7ee2add76fbbfb2205f81b4ce91ef913a17efff5f61ab2e` (el mismo que la 0.7.0, comprobado con `apksigner` en los dos). SHA-256 `435a6b6eae41a2608885a57e81d49123154a7523bf28cd5b8e084aa0af6fc5bb` |

Límites conocidos de la 0.8:

1. Nada probado en un teléfono: tacto del Piano Roll, atenuación con altavoz, cancelación de eco y latencia real de
   «Ajustar escuchando» en ARM.
2. Con altavoz no se puede eliminar el beat de la toma, solo reducirlo. La cancelación de eco del teléfono puede
   degradar la voz y no guarda copia sin procesar.
3. La regeneración de batería en trap, pop y electrónica cambia poco (parecido 0,72-0,81 frente a 0,75-0,83 al variar):
   la caja y el pulso del estilo se mantienen a propósito.
4. Hip-hop oscuro: el 58 % de la energía por debajo de 60 Hz; en altavoz de móvil sonará con poco grave.
5. Los medidores se consultan por sondeo: un pico muy corto puede no encender el indicador de saturación.
6. La sección «Grabación de voz» no tiene prueba de interfaz en la JVM.
7. Regenerar todas las fixtures de la JVM con `cs_render --fixtures05` rompe 8 pruebas antiguas; solo se actualizó `catalog.json`.

# Misión 0.7 «Channel Rack & Mixer Pro» (2026-11-13, rama `claude/project-thread-03ida9`, PR #3)

Mismas categorías que en la 0.6. «Render JVM» son las capturas de `capturas-0.7.0/` (40-46 son de esta misión).

| Entrega | Implementado | Probado | Compilado | Audio real | Render JVM | Físico |
|---|---|---|---|---|---|---|
| Solo AI: causa de «A-D suenan iguales» | Informe en SOLO_AI_ALTERNATIVES.md | `solo_ai_alternatives_reach_the_track…` (comandos de la app, A→B→C→D→A, audio en vivo) | — | MIDI y WAV de la 0.6 y la 0.7 en `muestras-0.7/` | — | Pendiente |
| Solo AI: perfiles A Sencilla, B Emotiva, C Rítmica, D Experimental (adaptador; núcleo intacto) | Sí | `solo_ai_profiles_have_measurable_identity…` (6 casos), `solo_ai_switching_while_playing…` | Sí | Envolvente entre alternativas 9,6-16,1 dB (0.6: 6,7-9,8). Solapamiento máx. 0,17 | `24`, `29` | Pendiente (**juicio de oído**) |
| Solo AI: «Escuchar sola», silenciar la melodía de la plantilla, textos | Sí | Comando de selección A→B→C→D→A en la JVM (`solo_pick_sequence…`) | Sí | — | Sí | Pendiente |
| Mixer Pro: rack de inserciones (añadir, controles, On/Off, subir/bajar, quitar), selector por categorías con descripción, VocalShift solo como «Procesar clip» | Sí | `mixer_slot_order_changes_processing…` (orden distinto: diferencia 0,168; bypass = sin efecto, exacto), `mixer_slot_edits_while_playing…`, 5 pruebas de interfaz | Sí | — | `44`, `45`, `05b` | Pendiente |
| Cadenas: Voz urbana, Voz limpia, Segunda voz, Trap 808, Batería, Melodía espacial | Sí | `mixer_chain_preset_applies_order_and_params_undoes_whole_and_survives_reopen` | Sí | — | `46` | Pendiente |
| Cadena «Master ligero» | **No**: el master no tiene inserciones en el motor | — | — | — | — | — |
| Channel Rack: pasos (16/32, páginas de 8 en vertical), intensidad, swing, patrones, filas melódicas → Piano Roll, canal del Mixer de cada fila | Sí | 5 pruebas de interfaz (`channel_rack_*`, `melodic_rows_*`, `drum_piece_*`) | Sí | Usa los comandos de la batería ya probados | `40`-`43` | Pendiente |
| Pieza de batería en su propio canal del Mixer (formato v3) | Sí | `channel_rack_drum_piece_on_own_channel_is_real_routing`: sin tocar nada, diferencia 0; fader, mute, solo e inserciones afectan solo a la pieza; vivo = exportación (0) | Sí | Prueba nula de stems: 0,00e+00 | `41` | Pendiente |
| Migración v2 → v3 | Sí | `channel_rack_v2_project_opens_unchanged_and_keeps_its_original` | Sí | — | — | Pendiente |
| Navegación Arreglo · Rack · Piano roll · Mixer · Sonido | Sí | `bottom_navigation_…` | Sí | — | Sí | Pendiente |
| VocalShift: refinamiento | **No cambiado**. No había voz humana con licencia para comparar (Wikimedia bloqueado); cambiar el DSP sin escuchar habría sido especular. Los controles de volumen, panorámica, retardo, espacio y A/B igualado ya existían | — | — | — | — | — |
| Identidad visual Dark Rose / Neon Violet para VocalShift | **No** | — | — | — | — | — |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **139/139** |
| ASan + UBSan | **139/139** prueba por prueba; 136 en la primera pasada. Las 3 restantes, explicadas y repetidas: `daw_export_with_drum_piece…` chocó con una ejecución mía simultánea en la misma carpeta temporal (repetida sola: correcta); `fuzz_command_api…` con 300 iteraciones solo falla su recuento mínimo, sin informe del sanitizador (repetida con 1500: correcta, 0 respuestas inválidas); `solo_ai_profiles…` destapó un defecto real (ver SOLO_AI_ALTERNATIVES.md §6), corregido y repetidas las 11 pruebas `solo_ai`: correctas. Sin informes de ASan ni UBSan |
| TSan | **139/139** prueba por prueba, sin avisos de carrera; misma salvedad del fuzz (repetido con 1500 iteraciones: correcto). Tras la corrección de Solo AI se repitieron las 11 pruebas `solo_ai`: correctas |
| ARM64 | Solo compilación (arm64-v8a dentro del APK) |
| Android JVM y Robolectric | **78/78** |
| Lint | 0 errores, 37 avisos y 9 sugerencias (no ocultados) |
| CI (GitHub Actions) | Trabajo `android` en verde desde `28e541a`. Trabajo `core` (Release + ASan) en rojo en `28e541a` y `f6f4ccb` por el mismo fallo de `solo_ai_profiles…` que vimos con ASan aquí (salto de D en el caso 4); corregido en el commit siguiente. **En `551b40e`: `core` y `android` en verde** (push y pull_request). |
| APK `caucesound-0.7.0-rack-mixer-preview.apk` | versionCode 9, versionName `0.7.0-rack-mixer-preview`, `com.caucesound.app.preview`, arm64-v8a y x86_64, certificado `56b4de46571e04c4e7ee2add76fbbfb2205f81b4ce91ef913a17efff5f61ab2e` (el mismo que la 0.6, comprobado con `apksigner`). SHA-256 `a7456d7dc21f4b347ceae2f368596feac624b816bfa12b820da750748ffd42a8` |

Límites conocidos de la 0.7:

1. Nada probado en un teléfono desde la 0.3.0.
2. Los proyectos guardados con la 0.7 no se abren en la 0.6 (formato v3). Queda la copia `project.v2.json`.
3. Sin inserciones en el master y sin efectos de modulación (chorus, flanger): la categoría no se muestra porque no hay ninguno real.
4. El *soft-clip* de la batería no actúa sobre una pieza enrutada a su canal.
5. Detector de tonalidad de Solo AI poco fiable (sin cambios).
6. En horizontal, el Rack muestra pocas filas a la vez por la altura (411 dp): se desplaza.
7. El Mixer no se desplaza solo hasta la tira elegida al llegar desde el Rack; el rack de efectos sí muestra ese canal.

# Misión 0.6 «Professional Production» (2026-11-12, rama `claude/project-thread-hxmmji`)

Categorías: **Implementado** (código en la rama), **Probado** (prueba automática en Linux x86-64), **Compilado**
(entra en el APK arm64), **Audio real** (medido sobre audio renderizado por el motor; no es escucha humana),
**Render JVM** (pantalla dibujada con Robolectric y revisada), **Físico** (en un teléfono: hoy, nada).

| Entrega | Implementado | Probado | Compilado | Audio real | Render JVM | Físico |
|---|---|---|---|---|---|---|
| A/B de VocalShift: el igualado de volumen es solo de escucha y nunca llega a la exportación (defecto de la 0.5) | Sí | `vocalshift_level_match_is_listen_only…` | Sí | Exportación −16,12 dB con y sin igualado | Sí | Pendiente |
| Prueba rápida de VocalShift (unos segundos sin tocar el proyecto) | Sí | `vocalshift_preview_*` (2) | Sí | Parecido 0,967 con el procesado completo | Sí | Pendiente |
| Retardo de la segunda voz aplicado desde el inicio (antes tardaba hasta 3 s) | Sí | `vocalshift_keeps_attacks…`: pedir 60 ms da 60,0 ms (antes 9,8 ms) | Sí | Voz sintética | — | Pendiente |
| Evaluación técnica de VocalShift: ataques, consonantes, silencios, extremos, determinismo | — | Misma prueba: ataques 0 ms, consonantes +2,5 dB, pausas 31,6 dB bajo la voz, reprocesado idéntico | — | Voz sintética, no humana | — | Pendiente |
| Paquete para DAW (stems con o sin efectos, MIDI, mezcla, proyecto, manifiesto con SHA-256) | Sí | `daw_*`, `smf_*`, `sha256_*` (5) + verificación independiente con `mido` | Sí | Suma de stems = mezcla (diferencia 0 en coma flotante, 3 LSB a 24 bits) | `26`-`28` | Pendiente |
| Programa de escritorio `cauce` | Sí | Usado en las pruebas y en las muestras | Solo Linux | — | — | — |
| Sonido 3.0 (mezcla de plantillas, filtro antes de la reverb, compás de cierre, melodía con intención) | Sí | 3 pruebas nuevas en `TestComposer` | Sí | Graves <250 Hz: 88→71 %, 91→77 %, 94→80 %. Melodía de −25 a entre −11 y −17 dB respecto a la batería | — | Pendiente (**es un juicio de oído**) |
| Solo AI 2.1: cinco ajustes de la melodía, rehacer solo el final, nivel de entrada según la mezcla | Sí | `solo_ai_transforms…`, `solo_ai_new_ending…` | Sí | — | `29` | Pendiente |
| Medición del análisis de Solo AI con 15 canciones propias | — | Tempo: 13 exactos, 15 exactos o mitad/doble. Tonalidad: reconocida en 3, exacta en 1, **1 caso equivocado con confianza 0,68** | — | Material propio renderizado, no música comercial | — | Pendiente |
| Idea rápida (un toque: proyecto, pista armada, grabación) | Sí | 1 prueba de interfaz; la grabación en sí usa el camino de siempre | Sí | — | `30` | Pendiente |
| Sampler | **No** | — | — | — | — | — |
| Cauce Link y CauceSound Studio | **Solo documentos** (CAUCE_LINK.md, CAUCESOUND_STUDIO.md) | — | — | — | — | — |

| Conjunto | Resultado |
|---|---|
| Motor x86-64 Release | **128/128** |
| ASan + UBSan | **128/128** prueba por prueba sobre el código de la entrega, sin informes. 127 con el fuzz de comandos reducido a 300 iteraciones (ahí solo falló su comprobación de recuento mínimo, no hubo informe del sanitizador) y ese fuzz repetido aparte con 1500 iteraciones: correcto |
| TSan | **128/128** prueba por prueba, sin avisos, con la misma salvedad del fuzz (repetido aparte con 1500 iteraciones: correcto) |
| ARM64 | **Solo compilación** (NDK, arm64-v8a dentro del APK). Las pruebas **no** se ejecutaron en ARM64 |
| Android JVM y Robolectric | **66/66** |
| Lint | 0 errores, 38 avisos (no ocultados) |
| CI (GitHub Actions) | Trabajo `android` en verde en el commit `f940dde`; el trabajo `core` (Release + ASan) seguía en curso al cerrar. En verde completo en `d6d7565`, `85cf2e7` y `9c92250` |
| APK `caucesound-0.6.0-production-preview.apk` | versionCode 8, `com.caucesound.app.preview`, arm64-v8a y x86_64, certificado `56b4de46…61ab2e` (el mismo que la 0.5.0, comprobado con `apksigner` en los dos APK). SHA-256 `c5da72cfd6deaefb3281c0e2e650ea97f512c54063c8f0a221ed8a860cf48f7e` |

Límites conocidos de la 0.6:

- **Nada probado en un teléfono desde la 0.3.0**: cuatro entregas acumuladas. Ver SAMSUNG_ACCEPTANCE_0.6.md.
- **Sonido**: lo demostrado es una diferencia medible, no una mejora. Solo afecta a canciones nuevas. Un intento de
  saturar el bajo para que se oiga en altavoces pequeños se retiró: no añadía armónicos, solo volumen. No hay
  compresión lateral (sidechain) entre bombo y bajo.
- **Tonalidad de Solo AI**: poco fiable con mezclas completas; en un caso (canción con cambio de energía) dio Si menor
  con confianza 0,68 siendo Re menor. Queda como defecto conocido medido en la prueba, sin corregir. La app avisa.
- **Paquete para DAW**: entre 60 y 180 MB por canción (WAV estéreo de 24 bits sin comprimir). No es un proyecto de
  FL Studio ni de ningún otro DAW: llegan audio y notas, no instrumentos ni efectos. La automatización no se exporta
  (no existe en la app).
- **VocalShift**: la naturalidad sigue sin juzgar (no hay voz humana aquí). El igualado A/B usa RMS, no LUFS.
- **Escritorio**: `cauce` solo se ha compilado en Linux. En Windows fallarían las rutas con acentos hasta adaptarlas.
- **Sampler**: no hecho; se priorizó lo anterior. **Idea rápida**: depende de la grabación, sin validar en teléfono.

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
