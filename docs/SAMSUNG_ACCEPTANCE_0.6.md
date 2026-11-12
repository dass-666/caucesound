# Prueba de aceptación en Samsung: CAUCESOUND 0.6 (Professional Production Preview)

**Ninguna de estas pruebas se ha hecho.** Este entorno no tiene teléfono ni emulador. Todo lo que la 0.6 afirma viene
de pruebas automáticas en un ordenador (Linux x86-64). Las cinco experiencias de abajo son la única forma de saber si
la app funciona de verdad, y solo las puede hacer Diego. La última versión probada en el teléfono fue la 0.3.0.

## Antes de empezar

1. Instala `caucesound-0.6.0-production-preview.apk` **encima** de la Preview que ya tengas. **No desinstales**: al
   desinstalar se borran tus proyectos. Si Android rechaza la instalación, para y anota el mensaje exacto.
2. Abre un proyecto antiguo y comprueba que sigue ahí y suena. Si falta alguno, no sigas: avísame.
3. Usa auriculares con cable para las experiencias 2 y 4 (el altavoz del teléfono no reproduce graves y el Bluetooth
   añade retraso).
4. Ten a mano un cronómetro (el del propio teléfono no sirve mientras la app está en pantalla: usa un reloj).

## Qué anotar en cada experiencia

| Dato | Cómo |
|---|---|
| **Resultado** | Bien / Con problemas / Falla |
| **Captura o audio** | Captura de pantalla (botón de encendido + bajar volumen) o el archivo exportado |
| **Error concreto** | El texto exacto del mensaje, o qué esperabas y qué pasó |
| **Tiempo aproximado** | Lo que tardó cada espera (análisis, procesado, exportación) |
| **Reproducción** | ¿Pasa siempre, a veces, una sola vez? Pasos exactos para repetirlo |
| **Diagnóstico** | Ajustes (engranaje) → línea «Motor de audio»: cópiala o haz captura justo después del fallo |

Si la app se cierra sola: anota qué estabas tocando en ese momento y vuelve a abrirla; di si el proyecto se recuperó.

---

## Experiencia 1: Estabilidad (unos 10 minutos)

Comprueba el fallo de la 0.3.0: Play dejaba de funcionar al cambiar de proyecto.

1. Crea tres canciones desde el inicio: Trap, Reggaetón y Hip-hop. En cada una pulsa Play, escucha 10 segundos, Stop.
2. Sal al inicio y abre la primera. Play. Sal. Abre la segunda. Play. Sal. Abre la tercera. Play.
3. Repite el paso 2 cinco veces seguidas, sin cerrar la app.
4. Con una canción sonando, pulsa el botón de inicio del teléfono, espera 10 segundos y vuelve a la app. Play otra vez.
5. Con una canción sonando, conecta y desconecta los auriculares.

Anota además: ¿en qué vuelta falló, si falló? ¿Suena siempre desde donde debe? ¿Algún corte o chasquido?

## Experiencia 2: Calidad musical (unos 15 minutos)

La 0.6 cambia la mezcla de las plantillas: menos peso de graves, melodía más presente, compás final de cierre.
**Solo afecta a canciones nuevas**; las antiguas suenan como antes.

1. Abre una canción de Trap creada con una versión anterior y escúchala entera con auriculares.
2. Crea una canción de Trap nueva y escúchala entera.
3. Compara: ¿se oye la melodía? ¿El 808 sigue teniendo fuerza o se ha quedado flojo? ¿Los platos molestan?
   ¿La canción termina o se corta?
4. En la nueva, usa las variaciones (✨): bloquea el bajo con el candado y pide otra versión dos veces.
5. Repite 2 y 3 con Reggaetón y Hip-hop si tienes tiempo.

Anota además: cuál prefieres (anterior o nueva) en cada estilo y por qué, con tus palabras. «Suena más profesional»
o «suena peor» es una respuesta válida y es justo la que no puedo dar yo: mis medidas dicen que la melodía subió y los
graves bajaron, no que suene mejor. Hay muestras de antes y después en `muestras-0.6` para comparar también en el PC.

## Experiencia 3: Solo AI (unos 15 minutos)

1. Proyecto vacío. Importa un beat real (WAV o MP3 de 1 a 4 minutos) del que **sepas** el tempo y la tonalidad.
2. Toca el clip → Solo AI → «Analizar». Cronometra.
3. Anota el BPM y la tonalidad que propone y la confianza que indica. Compáralos con los reales.
4. Corrige a mano lo que esté mal y confirma.
5. «Generar 4 alternativas». Escucha A, B, C y D sobre el beat.
6. Elige una. Prueba «Ajustar esta melodía»: Menos notas, Más intensa, Más suave, Octava arriba, Cambiar el final.
   Después de cada una, Deshacer.
7. Ábrela en el Piano roll y mueve dos notas.

Anota además: BPM real y detectado; tonalidad real y detectada. **Aviso**: en mis medidas con 15 canciones propias el
tempo acertó (o dio la mitad o el doble) en las 15, pero la tonalidad solo se reconoció en 3 y en un caso dio una
equivocada con confianza media. Espero que falle a menudo con beats reales; por eso la app pide que la confirmes.
¿Las cuatro alternativas son distintas de verdad? ¿Alguna encaja? ¿Los botones de ajuste hacen lo que dicen?

## Experiencia 4: VocalShift (unos 15 minutos)

1. Desde el inicio toca **«Idea rápida»**: debe pedir permiso de micrófono (la primera vez) y empezar a grabar.
   Canta o habla 15 a 20 segundos con auriculares puestos. Stop.
2. Toca el clip → VocalShift → preset «Urbano Espacial — Segunda voz».
3. «Prueba rápida» → «Escuchar prueba». Cronometra. ¿Oyes la segunda voz en ese trozo?
4. «Procesar voz». Cronometra.
5. «Escuchar» y alterna «A · Original» y «B · Procesada» con «Igualar volumen» activado y desactivado.
6. En Avanzado, sube «Retardo de la segunda voz» a 60 ms y vuelve a procesar: la segunda voz debe ir claramente
   detrás desde la primera sílaba (en la 0.5 el retardo tardaba segundos en aparecer).
7. Acepta. Exporta la mezcla (experiencia 5) y comprueba en el PC que la voz está a tiempo.

Anota además: **naturalidad** (natural, robótica, metálica, con eco raro) y **sincronía** (la segunda voz entra a la
vez, antes o después). ¿Las consonantes (s, t, k) se entienden? ¿Hay ruido en los silencios entre frases? Es la
valoración que no puedo hacer: aquí no hay voz humana ni oídos, solo medidas con voz sintética.

## Experiencia 5: Exportación (unos 15 minutos, con un PC con Windows)

1. En una canción de Trap nueva con una pista de Solo AI: Exportar → «Exportar mezcla». Cronometra.
2. Exportar → «Paquete para DAW (ZIP)» → «Con efectos» → «Crear paquete». Elige la carpeta Descargas. Cronometra y
   anota el tamaño del archivo. Mientras exporta, comprueba que hay barra de progreso y botón Cancelar.
3. Repite y pulsa **Cancelar** a mitad: no debe quedar ningún ZIP a medias en Descargas.
4. Pasa el ZIP al PC (cable USB o Drive). Descomprímelo. Debe contener `stems`, `midi`, `mix`, `project`,
   `manifest.json` y `README.txt`.
5. **Integridad**: abre PowerShell en la carpeta y ejecuta `Get-FileHash .\mix\*.wav -Algorithm SHA256`. Compara con
   el valor `sha256` de ese archivo en `manifest.json` (ábrelo con el Bloc de notas).
6. **Sincronía**: arrastra todos los WAV de `stems` a un DAW (FL Studio, Reaper, Audacity vale) empezando todos en el
   tiempo 0. Deben sonar igual que la mezcla. Arrastra también el `.mid`: las notas deben caer sobre el audio.

Anota además: tiempos y tamaño; si el hash coincide; si algún stem empieza tarde o dura distinto; si el MIDI trae una
pista por instrumento con su nombre; si la batería llega por el canal 10.
**Qué no esperar**: no es un proyecto de FL Studio. Llegan audio y notas, no los instrumentos ni los efectos.

---

## Cómo devolverme los resultados

Una lista con las cinco experiencias y los seis datos de cada una basta. Lo más útil, por orden: (1) si Play aguanta la
experiencia 1, (2) qué prefieres en la experiencia 2, (3) la naturalidad de la experiencia 4.
