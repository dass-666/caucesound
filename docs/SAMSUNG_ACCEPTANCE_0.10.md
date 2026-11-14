# Pruebas en el Samsung — 0.10 (Preview, versionCode 15)

Nada de esta versión se ha probado en un teléfono, y la 0.9.2 tampoco. Instala
`caucesound-0.10.0-sound-engine-preview.apk` encima de la Preview que tengas: la firma es la misma y los proyectos se
conservan. Tiempo estimado: 30-40 min. Si tienes poco tiempo, haz las pruebas 1 a 7.

Anota en cada una si funciona, si falla, o si funciona pero suena raro, y describe lo que oíste. En las pruebas de
sonido lo que cuenta es tu oído: en la nube solo se pudo medir.

| # | Prueba | Cómo | Qué fijarse |
|---|---|---|---|
| 1 | Proyectos anteriores | Abre un proyecto de la 0.6 o la 0.9 y dale Play | Suena igual que antes |
| 2 | Plantilla de siempre | Inicio, con «Signature 2» **apagado**: crea un Trap | Es el Trap de siempre (mismo sonido y duración) |
| 3 | Signature 2 | Enciende «Signature 2 (en pruebas)» y crea Trap, Reggaetón, Boom Bap y Hip-hop | Se crea sin avisos de sonidos que faltan. Hay Intro, Verso, Coro, Verso 2, Coro 2. **Juicio de oído**: ¿la batería tiene más carácter?, ¿cada género suena distinto?, ¿el 808 se oye en el altavoz del teléfono?, ¿el balance sigue siendo el que te gustaba? |
| 4 | Comparación directa | Escucha en el teléfono los WAV de `muestras-0.10` (1 frente a 2, y 2 frente a 3) | Cuál prefieres en cada estilo, con altavoz y con auriculares |
| 5 | Humanizar | Variaciones → Humanizar: Cuadrado, Natural, Suelto | Se nota la diferencia sin que parezca un error de tiempo. Deshacer vuelve atrás |
| 6 | Variar y Regenerar en Signature 2 | Variar todo y Regenerar beat | Funcionan igual que en la 0.9.2 y mantienen los sonidos nuevos |
| 7 | Cambiar de proyecto con sonidos cargados | Abre una canción Signature 2, Play, sal, abre otra, Play; repite 5 veces | Play siempre funciona, sin cortes ni cierres |
| 8 | Sonidos nuevos en el explorador | Explorador de sonidos → Drums II, Bass Lab II, Keys II | Aparecen, se pueden escuchar y cargar en una pista |
| 9 | AutoMix: nada que cambiar | Canción recién creada → Mixer → AutoMix | Dice que no hay nada que cambiar |
| 10 | AutoMix: desequilibrio | Sube mucho los acordes y el máster → AutoMix → Aplicar | Propone bajar los acordes y el máster. Con «Comparar» alternas entre antes y después; «Revertir» deja lo de antes |
| 11 | AutoMix con voz | Graba una voz bajita sobre un Jazz Rap → AutoMix | Propone bajar los teclados «para dejar sitio a la voz». **Juicio de oído**: ¿la voz se entiende mejor? |
| 12 | Diagnóstico de VocalShift en vivo | Con la música parada: Ajustes → Diagnóstico → «Medir VocalShift en vivo (5 s)» | **Apunta las cifras** (CPU, peor bloque, arranque). No activa nada. Con ellas se decide si VocalShift podrá funcionar durante la reproducción |
| 13 | Tamaño e instalación | Instalar encima | Se instala sin desinstalar la anterior. El APK pesa más que la 0.9.2 por los sonidos nuevos |
| 14 | Lo pendiente de la 0.9.2 | Guía `SAMSUNG_ACCEPTANCE_0.9.2.md` | Sigue sin validar: Variar en el altavoz, plugins en el Mixer, grabación de un toque |

Las pruebas 3, 4 y 12 son las que más ayudan a decidir el siguiente paso.
