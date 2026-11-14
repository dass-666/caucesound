# Pruebas en el Samsung — 0.9.2 hotfix (Preview, versionCode 14)

Nada de esta versión se ha probado en un teléfono. Tiempo estimado: 15-20 min. Instala
`caucesound-0.9.2-hotfix-preview.apk` encima de la Preview anterior. La firma es la misma, así que los proyectos se
conservan. Prueba primero con el **altavoz del teléfono**, que es donde antes «no pasaba nada», y después con
auriculares.

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| 1 | Proyectos anteriores | Abre un proyecto de la 0.9.1 y dale Play | Suena igual que antes |
| 2 | Variar todo en Electrónica | Plantilla Electrónica → Variaciones → «Variar todo», con el beat parado | Aparece «Generando…» y después «Versión 2», con el porcentaje de cambio por parte. «Escuchar» reproduce desde la sección A y el cambio **se nota en el altavoz** |
| 3 | Durante la reproducción | Con Play en marcha, pulsa «Regenerar beat» | El beat cambia claramente en unos segundos, sin cortes ni chasquidos |
| 4 | Cada parte | Rehacer o Variar Batería, Bajo, Acordes, Pad y Melodía, uno a uno | Cada botón cambia **esa** parte de forma audible, incluido el Pad |
| 5 | Comparar y Deshacer | Anterior/Siguiente y luego Deshacer | Vuelves a la versión anterior exacta |
| 6 | Candado | Bloquea el Bajo y pulsa «Variar todo» | El bajo no cambia y la tarjeta dice que se respetó. Si bloqueas todo, aparece un mensaje que explica qué hacer |
| 7 | Guardar | Guarda, cierra y reabre el proyecto | Se conserva la última versión |
| 8 | Repite 2-4 en Trap, Pop y Dark Hip-hop | Igual que antes | El cambio se nota en los tres estilos |
| 9 | Plugins en el Mixer | Mixer → toca la parte de arriba de un canal → «Agregar plugin» | Aparece la cadena del canal y un catálogo por categorías. Al elegir, por ejemplo, Reverb, sus controles se abren solos |
| 10 | Ajustar escuchando | Con Play, mueve un control. Después desactiva, reordena y borra el efecto | El cambio se oye al momento y no hay ruidos |
| 11 | VocalShift en el Mixer | Canal de una pista de audio con grabación → Agregar plugin → Procesamiento vocal | Aparece «VocalShift: procesar la grabación (no en vivo)» y abre su panel con la toma. En una pista de sintetizador no aparece |
| 12 | Grabación de un toque | Proyecto sin pista de voz → botón rojo | Pide crear una pista «Voz» (y el permiso del micrófono la primera vez) y graba |
| 13 | Sin acompañamiento | Repite la 12 activando «Grabar sin acompañamiento» | Durante la toma no suenan el beat ni el metrónomo. Al parar y dar Play, la mezcla suena como antes y la voz está alineada |
| 14 | Multipista | Arma dos pistas de audio y pulsa el botón rojo | Graba directamente, sin diálogo, como antes |

Anota en cada prueba si funciona, si falla, o si funciona pero suena raro, y describe lo que oíste. Las pruebas 2, 3,
4 y 8 son las que confirman el arreglo principal.
