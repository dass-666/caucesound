# Pruebas en el Samsung — 0.9.0 «Signature Sounds» (Preview, versionCode 12)

Nada de esta versión se ha probado en un teléfono. Estas pruebas son las que importan. Tiempo estimado: 25-30 min.
Instala `caucesound-0.9.0-signature-sounds-preview.apk` encima de la Preview anterior (misma firma: los proyectos se
conservan). Primero abre un proyecto de la 0.8 y comprueba que suena igual.

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| 1 | Proyectos anteriores intactos | Abre un proyecto creado con la 0.8, dale Play | Suena igual que antes; no aparece ningún aviso |
| 2 | Primer arranque | Abre la app recién instalada (primera vez tarda algo más: copia 16 MB de sonidos) | Arranca en menos de ~3 s; si tarda más, anótalo |
| 3 | Explorador de kits | Plantilla Boom Bap → Rack → icono de nota (Kits de muestras) | Aparecen 3 kits con estilo, origen y descripción |
| 4 | Escuchar | Toca ▶ en «Cauce Dark Trap» con el transporte parado | Suena una frase corta de batería, sin cortes ni clics |
| 5 | Cargar kit | «Usar en Drums», luego Play | El beat suena con el kit nuevo; el Rack muestra el nombre del kit |
| 6 | Mismo MIDI, otro kit | Cambia a Raw Boom Bap y a Latin Pulse con Play en marcha | Cambia el sonido, el ritmo sigue igual; sin silencio largo ni chasquidos |
| 7 | Deshacer | Deshacer tras cambiar de kit | Vuelve el kit anterior |
| 8 | Volver al kit sintetizado | Menú del kit en la rejilla → elige un kit clásico | Vuelve el sonido de la 0.8 |
| 9 | Bajo eléctrico | Pista «Bajo» → Sonido → icono de nota → «Cauce Electric Bass» → Usar | Se oye un bajo de verdad (dedos). Toca fuerte y suave en el teclado: cambia el timbre, no solo el volumen |
| 10 | 808 Lab | Igual con «Cauce 808 Lab»; toca dos notas ligadas | El 808 se desliza de una nota a otra; en el altavoz del móvil se oye (si no, anótalo) |
| 11 | Ajustes del sampler | Mueve Tone, Drive, Release | Cambios audibles y suaves, sin ruidos |
| 12 | Buscar | En el explorador escribe «808», luego «electrico» | Encuentra 808 Lab (y Dark Trap por su etiqueta) y el bajo eléctrico |
| 13 | Favoritos | Estrella en un sonido, filtro ★ | Aparece en favoritos; sigue ahí tras cerrar la app |
| 14 | Importar piano | Copia `cauce.keys.grand-1.0.0.zip` al teléfono → Sonidos → Paquetes → Importar | Se instala (42 MB); aparece «Cauce Grand Piano» en Teclados |
| 15 | Piano | Pista de acordes → Cauce Grand Piano → toca acordes | Suena un piano real; anota si las notas largas se cortan pronto (están recortadas a 1,4-3 s) |
| 16 | Borrar paquete | Paquetes → papelera en Grand Piano | Pregunta y dice qué proyectos lo usan; tras borrar, ese proyecto avisa de «sonidos que faltan» y no suena esa pista, sin perder notas |
| 17 | Reimportar | Vuelve a importar el piano y abre ese proyecto | La pista vuelve a sonar sola |
| 18 | ZIP malo | Intenta importar un ZIP cualquiera (fotos, etc.) | Mensaje claro de error; nada se rompe |
| 19 | AutoMix | Sube mucho el volumen de una pista → Mixer → ✨ | Propone bajarla y explica por qué; Aplicar y luego Revertir dejan todo como estaba |
| 20 | AutoMix en plantilla sin tocar | Proyecto nuevo → Mixer → ✨ | Dice que no hay nada que cambiar |
| 21 | Regenerar en Electrónica | Plantilla Electronic → Variaciones → Regenerar varias veces | Cambia el pie del beat de forma audible (breakbeat, medio tiempo…) |
| 22 | Cambio de proyecto | Abre proyecto con kit de muestras, sal, abre otro, vuelve | Play funciona siempre (el fallo de la 0.3.0 no vuelve) |
| 23 | Memoria | Con piano + bajo + kit cargados, usa la app 5 min | Sin cierres ni lentitud |
| 24 | Exportar | Exporta WAV y paquete para DAW de un proyecto con sonidos 0.9 | Los sonidos nuevos están en la mezcla y en los stems |

Anota para cada prueba: bien / mal / dudoso, y si algo suena mal, en qué altavoz (teléfono o auriculares).
