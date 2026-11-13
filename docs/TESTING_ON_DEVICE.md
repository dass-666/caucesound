# Ejecutar CAUCESOUND en un Samsung desde Windows

## Fase 1.2: instalar «CAUCESOUND Preview» junto a la app de la fase 1

**Archivo**: `caucesound-0.2.0-fase1.2-preview.apk` (versión 0.2.0-fase1.2-preview, código 2).
SHA-256 del APK: se indica en el informe de la entrega; compruébalo en PowerShell con
`Get-FileHash .\caucesound-0.2.0-fase1.2-preview.apk`.

**Por qué es una app aparte**: el APK de la fase 1 se firmó con una clave de depuración temporal que se perdió al
cerrarse aquel entorno. Android solo permite actualizar una app con la **misma** firma, así que una versión nueva
no puede sustituirla sin desinstalarla, y desinstalarla **borra sus proyectos** (se guardan dentro de la app).
Para no perder nada, esta versión se instala **al lado**, con otro identificador (`com.caucesound.app.preview`) y el
nombre «CAUCESOUND Preview». A partir de ahora se firma con una clave de prueba persistente (guardada fuera del
repositorio, en la carpeta del proyecto), así que las próximas versiones Preview sí se instalarán encima conservando
los proyectos.

1. Sigue los pasos 1-3 de la «Opción rápida» de abajo (depuración USB y platform-tools) si no los tienes ya.
2. Instala:
   ```powershell
   .\adb install caucesound-0.2.0-fase1.2-preview.apk
   ```
   (o copia el APK al teléfono y ábrelo desde «Mis archivos»). Verás dos iconos: CAUCESOUND (fase 1) y
   CAUCESOUND Preview.
3. **Proyectos antiguos**: siguen en la app de la fase 1, intactos. La Preview no puede leerlos (Android aísla los
   datos de cada app). Si quieres uno en la Preview: en la app antigua, Exportar WAV; en la Preview, crea un
   proyecto e importa ese WAV en una pista de audio. El formato nuevo sí abre proyectos de la fase 1 (se prueba
   automáticamente), así que cuando haya una vía de copia de proyectos (fase 2) se podrán traer completos.
4. No desinstales la app de la fase 1 hasta haber rescatado lo que quieras de ella.

#### Rescatar proyectos de la fase 1

La Preview ya exporta e importa proyectos completos (`.caucesound`), pero la app de la fase 1 no tiene esa opción y
no se puede actualizar. Comprobé el APK de la fase 1: **no es depurable** y desactiva las copias de seguridad de sus
datos, así que no hay vía con `adb` (`run-as`, `adb backup`) para sacar sus carpetas. La vía que sí funciona es
por pistas: en la app de la fase 1, para cada pista activa **Solo** y exporta el WAV. Así obtienes una pista por
archivo, con su posición desde el inicio (comprobado en el código de la fase 1: la exportación respeta Solo y
empieza en el compás 1; no probado en el teléfono). Luego, en la Preview, crea un proyecto e importa cada WAV en su propia
pista de audio, en la posición 0. Se pierden los ajustes de clip (cortes y fundidos ya van aplicados en el audio),
pero no el sonido.

### Protocolo de prueba de la fase 1.2

Anota modelo, versión de Android y si usas auriculares con cable, Bluetooth o altavoz. Lo más útil son los fallos:
envíalos tal cual (qué hiciste, qué esperabas, qué pasó). Si algo se cuelga, `.\adb logcat -s CauceSound` ayuda.

| # | Prueba | Cómo | Qué anotar |
|---|---|---|---|
| 1 | Inicio | Abre la Preview. Toca la plantilla **Trap** | ¿Se crea y abre? ¿Ves Drums, 808, Melodía, Voz? |
| 2 | Reproducir | Play | ¿Suena el beat con bajo y arpegio? ¿Clics, cortes, desfase entre batería y synths? |
| 3 | Bucle | Toca el icono de bucle (y pulsación larga para cambiar compases) | ¿Repite exactamente sin hueco ni golpe doble? |
| 4 | Batería | Pestaña Batería: activa/desactiva pasos mientras suena; cambia a 32 pasos; swing al 50 % | ¿Responde al instante? ¿El swing se nota? |
| 5 | Pads | En Batería → Pads, toca con varios dedos a la vez | ¿Latencia perceptible? (nada / poca / molesta). ¿Suenan los dos golpes simultáneos? |
| 6 | Kits | Cambia entre Trap, Reggaetón, Hip-hop y Electrónica | ¿Suenan distintos? ¿Algún sonido desagradable o muy fuerte? |
| 7 | Synth | Pestaña Synth: prueba los 7 presets con el teclado; varios dedos a la vez | ¿Notas colgadas al soltar? ¿Latencia? |
| 8 | Piano roll | Pestaña Piano roll: dibuja notas, muévelas, alarga, doble toque para borrar, zoom con dos dedos | ¿Qué gesto cuesta o falla? |
| 9 | Notas colgadas | Mientras suena, para, mueve el cursor, cambia de pestaña, bloquea la pantalla | ¿Queda alguna nota sonando? |
| 10 | Grabar voz | En la pista Voz (R activado), pulsa grabar y canta 8 compases sobre el beat con auriculares con cable | ¿La voz queda alineada con el beat al reproducir? |
| 11 | Efectos | Mezclador → FX en Melodía → añade Reverb y Delay; gira perillas mientras suena; bypass | ¿Cambios sin clics? ¿Bypass funciona? |
| 12 | Mezcla | Mezclador: volumen, pan, mute, solo | ¿Responde sin saltos? |
| 13 | Guardar y reabrir | Guarda, vuelve al inicio, reabre | ¿Todo igual (patrones, notas, efectos, voz)? |
| 14 | Exportar | Exporta a 24 bits y ábrelo en Windows | ¿Incluye batería, synths, voz y la cola de la reverb al final? |
| 15 | Horizontal | Gira el teléfono en el Arreglo | ¿Se ve bien? ¿Cuántas pistas caben? |
| 16 | Carga | Añade 4 synths con el preset Pad y acordes largos + reverb en cada uno; reproduce 1 minuto y abre Ajustes | ¿Cortes? ¿Se calienta? Copia las líneas de XRuns y **Carga DSP** (media, pico desde la última vez que abriste Ajustes, bloques tarde). Repite añadiendo pistas hasta oír cortes: ese número de pistas es el límite real |
| 17 | Interrupciones | Durante la reproducción: llamada, alarma, desconectar auriculares | ¿Se detiene limpio? ¿Notas colgadas al volver? |
| 18 | Deshacer perillas | Gira el corte del synth varias veces y pulsa Deshacer | ¿Vuelve al valor anterior a cada giro (no paso a paso)? |
| 19 | Exportar/importar proyecto | Inicio → ⋮ de un proyecto → Exportar (.caucesound), guárdalo en Descargas; luego Importar ese archivo | ¿Aparece la copia y suena igual? ¿Se abre el archivo en Windows como ZIP? |
| 20 | Bus de reverb | Mezclador → «+ Bus de reverb». Sube la perilla Send de Melodía y de la caja/batería mientras suena; mute en Melodía; exporta | ¿Se oye la reverb solo en las pistas con Send? ¿Al silenciar Melodía desaparece también su reverb? ¿La exportación incluye la cola? Repite la prueba 16 con un bus en lugar de una reverb por pista y compara la Carga DSP |
| 21 | Perillas | Arrastra hacia arriba/derecha cualquier perilla (Pan, Send, synth) y haz doble toque sobre ella | ¿Gira con el dedo? ¿El doble toque la devuelve al valor por defecto? ¿Es cómodo el recorrido con el dedo? (Las pruebas JVM simulan el gesto, pero no la ergonomía) |

### Pruebas nuevas de la versión 0.3.0 (pulido)

| # | Qué | Cómo | Qué anotar |
|---|---|---|---|
| 22 | Actualización | Instala `caucesound-0.3.0-polish-preview.apk` encima de la Preview 0.2.0 (sin desinstalar) | ¿Se instala sin error? ¿Siguen tus proyectos de la Preview? |
| 23 | Cuenta previa | Toca el BPM → «Cuenta previa: 1 compás». Arma la pista de voz y graba | ¿Suenan 4 clics antes de empezar? ¿La posición se queda quieta durante la cuenta? ¿La toma queda alineada con la batería? Para durante la cuenta: ¿se cancela sin mensaje de error? |
| 24 | Tap tempo | Toca el BPM → pulsa «Tocar» 5 o 6 veces siguiendo una canción | ¿El BPM se parece al de la canción? |
| 25 | Perillas tras deshacer | En el synth mueve «Corte», pulsa deshacer y vuelve a moverla | ¿La perilla vuelve a su sitio al deshacer? ¿El segundo gesto empieza desde ahí, sin saltos? |
| 26 | Batería | Cambia de kit (¿suena bombo y caja de muestra?), varita → «Remate al final», deshacer | ¿Se oye el redoble? ¿Deshacer lo quita? |
| 27 | Piano roll | Elige escala «La menor», dibuja notas, arrastra las barras de intensidad de abajo, «Repetir ×2» | ¿Las filas fuera de escala se ven más oscuras? ¿Cambia la fuerza al oído? ¿Se duplica el clip? |
| 28 | Tamaños | Gira el teléfono en Arreglo, Mezclador y Piano roll | ¿Algún botón cortado, texto partido o control demasiado pequeño para el dedo? |
| 29 | 808 | Synth → Bajos → «808 Bass», toca notas graves | ¿Se oye el golpe inicial y la nota queda afinada? |

### Pruebas de la versión 0.3.1 (hotfix de Play)

Instala `caucesound-0.3.1-hotfix-preview.apk` encima de la 0.3.0 (sin desinstalar). Si algo falla, abre Ajustes →
Estado del audio y haz una captura: `outState`, `processedFrames` y `lastError` dicen si el stream sigue vivo.

| # | Qué | Cómo | Qué anotar |
|---|---|---|---|
| 30 | Secuencia del fallo | Proyecto A → Play (deja que pase el final, ~30 s) → salir sin Stop → Proyecto B → Play → salir → A → Play | ¿Suena siempre? ¿El cabezal empieza al principio en cada proyecto? |
| 31 | Play repetido | En un proyecto: Play, Stop, Play, Stop… 10 veces | ¿Arranca siempre y sigue sonando? (en la 0.3.0 se paraba al instante desde el segundo Play) |
| 32 | Stop antes de salir | Play → Stop → salir → otro proyecto → Play | ¿Suena? |
| 33 | Segundo plano | Play → botón de inicio del teléfono → volver a la app → salir del proyecto → abrir otro → Play | ¿Suena? |
| 34 | Girar | Play → gira el teléfono → salir → otro proyecto → Play | ¿Suena? ¿Se mantiene el estado? |
| 35 | Rápido | Abre y cierra el mismo proyecto 5 veces seguidas, luego Play | ¿Suena? ¿Algún mensaje de error? |
| 36 | Tras grabar | Graba una toma corta → Stop → salir → otro proyecto → Play → vuelve al de la toma | ¿Suena? ¿La toma sigue ahí? |
| 37 | Otra app | Play en CAUCESOUND → reproduce algo en otra app (YouTube, Spotify) → vuelve | CAUCESOUND debe pararse al empezar la otra app; al volver, ¿Play funciona? |

### Pruebas de la versión 0.4.1 (sonido, variaciones, Solo AI)

Instala `caucesound-0.4.1-soloai-preview.apk` encima de la anterior (sin desinstalar). Repite antes las 30-37: la 0.4
cambia las plantillas y el fallo de Play no debe volver.

| # | Qué | Cómo | Qué anotar |
|---|---|---|---|
| 40 | Estilos nuevos | Crea una canción de Reggaetón, Trap y Hip-hop y escucha los 16 compases | ¿Cada estilo tiene identidad? ¿Algo suena mal, demasiado grave o apagado? (el hip-hop es el sospechoso) |
| 41 | Variaciones | Botón ✨ → «Otra versión completa» con Play en marcha; luego «Versión anterior» / «Versión nueva» | ¿Cambia sin cortes ni notas colgadas? ¿Se nota la diferencia? |
| 42 | Por parte y candado | Pon candado al bajo → «Batería» y «Melodía» varias veces | ¿El bajo no cambia nunca? |
| 43 | Intensidad | «Más tranquila» y «Más enérgica» | ¿La diferencia es clara? |
| 44 | Redoble | Rejilla de batería → «Redoble» → toca un charles varias veces | ¿Suenan 2, 3 y 4 golpes? ¿Se ven las marcas? |
| 45 | Solo AI con plan | ✨ → Solo AI → Generar (estribillo, 8 compases) | ¿Aparece la pista «Solo AI · …»? ¿Encaja con los acordes? ¿Tarda? |
| 46 | Solo AI sin plan | En un proyecto de la 0.3: ✨ → elige tonalidad → Generar | ¿Pide la tonalidad antes? ¿Suena en esa tonalidad? |
| 47 | Botón «Nueva canción» | Con 6 o más proyectos, baja hasta el final de la lista | ¿Se ve entero el último proyecto y su menú «⋮»? |
| 48 | CPU | Play de una plantilla 0.4 durante 5 min | ¿Cortes? ¿El teléfono se calienta más que con la 0.3? |

### Pruebas de la versión 0.5.0 (VocalShift, Solo AI 2.0, estabilidad)

Instala `caucesound-0.5.0-integration-preview.apk` encima de la 0.4.1 (sin desinstalar). Tus proyectos deben seguir
ahí: si no aparecen, **no desinstales** y avísame. Repite antes las 30-37 (Play tras cambiar de proyecto).

| # | Qué | Cómo | Qué anotar |
|---|---|---|---|
| 49 | Actualización | Instalar encima de la 0.4.1 y abrir un proyecto antiguo | ¿Siguen todos los proyectos? ¿Suenan igual? |
| 50 | Canción de Trap | Nueva canción → Trap → ✨ variaciones, candado al bajo, otra versión | ¿Funciona como en la 0.4? (no debe haber cambiado) |
| 51 | Analizar un beat | Proyecto vacío → importa un instrumental (MP3 o WAV de 1-4 min) → toca el clip → «Analizar» | Tiempo que tarda. BPM y tonalidad que propone y su confianza. ¿Aciertan? (compara con lo que sepas del beat) |
| 52 | Corregir el análisis | En la pantalla de Solo AI cambia la tonalidad o el BPM y confirma | ¿Se respeta lo que pones tú? ¿El tempo del proyecto solo cambia si lo aceptas? |
| 53 | Alternativas A-D | Solo AI → Generar → escucha A, B, C y D → elige una → «Variación» | ¿Las cuatro son distintas? ¿Encajan con el beat? ¿La elegida aparece como pista editable en el Piano roll? |
| 54 | VocalShift | Graba 15-20 s de voz con auriculares → toca el clip → «VocalShift» → preset «Urbano Espacial — Segunda voz» → «Procesar voz» | Tiempo que tarda. ¿Se oye la segunda voz? ¿Está a tiempo con la original (sin eco ni retraso)? |
| 55 | A/B | Con Play en marcha, pulsa «Escuchar» y alterna «A · Original» y «B · Procesada» | ¿Cambia sin cortar ni reiniciar? Con «Igualar volumen» activo, ¿suenan igual de fuertes? |
| 56 | Naturalidad | Escucha el procesado solo, con auriculares; prueba también ±2 y ±4 semitonos | ¿Suena natural, robótico, metálico? ¿Las consonantes se emborronan? Es la valoración que yo no puedo hacer |
| 57 | Original intacto | Acepta el procesado, luego «Descartar el procesado»; después deshacer y rehacer | ¿La toma original sigue exactamente igual? |
| 58 | Cancelar y cambiar | Procesa una toma de 1-2 min y pulsa Cancelar a mitad. Repite y, a mitad, sal al inicio y abre otro proyecto | ¿El proyecto queda como estaba? ¿El otro proyecto no recibe nada? ¿Play funciona después? |
| 59 | Exportar | Exporta la canción con la voz procesada y ábrela en el PC | ¿Voz y beat alineados? |
| 60 | Carga | Proyecto con beat importado + pista de Solo AI + 2 voces procesadas, Play 5 min | ¿Cortes? ¿Temperatura? Línea de «Motor de audio» de Ajustes |

### Pruebas de la versión 0.6.0

Están en un documento propio, pensado para seguirlo con el teléfono en la mano: **`SAMSUNG_ACCEPTANCE_0.6.md`**
(cinco experiencias: estabilidad, calidad musical, Solo AI, VocalShift y exportación). Sustituye a las listas
anteriores como punto de partida; las pruebas 30-60 siguen siendo válidas si quieres más detalle.

## Fase 1: opción rápida: instalar el APK ya compilado (sin Android Studio)

1. **En el teléfono**: Ajustes → Acerca del teléfono → Información de software → toca 7 veces «Número de
   compilación». Vuelve a Ajustes → Opciones de desarrollador → activa **Depuración USB**.
2. **En Windows**: descarga «SDK Platform-Tools for Windows» (gratis) desde
   https://developer.android.com/tools/releases/platform-tools y descomprímelo, por ejemplo en `C:\platform-tools`.
3. Conecta el teléfono por USB y acepta en el teléfono el aviso «¿Permitir depuración USB?».
4. Abre PowerShell en esa carpeta y ejecuta:
   ```powershell
   .\adb devices                 # debe aparecer el teléfono como "device"
   .\adb install -r caucesound-0.1.0-fase1.apk
   ```
   Alternativa sin cable: copia el APK al teléfono y ábrelo desde «Mis archivos» (Samsung pedirá permitir
   «instalar apps desconocidas» para esa app).
5. Abre CAUCESOUND. La primera vez que pulses grabar te pedirá permiso de micrófono.

Para ver el registro del motor mientras pruebas: `.\adb logcat -s CauceSound`

## Opción completa: compilar en Windows

1. Instala Android Studio (gratis). En *SDK Manager* instala: Android SDK Platform 36, NDK 27.2.12479018 y CMake 3.31.6.
2. `git clone https://github.com/dass-666/caucesound` y abre la carpeta `android/` en Android Studio.
3. Conecta el teléfono y en terminal: `cd android` y `.\gradlew.bat installPreview` (con la variable
   `CAUCE_SIGNING` apuntando a `keystore.properties` de la clave de prueba; sin ella se firma con tu clave de
   depuración local y no podrá actualizar la Preview instalada desde el APK). **No uses `installDebug` con la app
   de la fase 1 instalada**: comparte su identificador, Android rechaza la firma distinta
   (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`) y la única salida sería desinstalarla, lo que borra sus proyectos.
4. Pruebas del motor en Windows (opcional, con Visual Studio 2022 y CMake):
   ```powershell
   cmake -S . -B build
   cmake --build build --config Release
   .\build\Release\cs_core_tests.exe
   ```
   (El motor se ha probado en Linux; en Windows/MSVC todavía no se ha compilado.)

## Protocolo de validación física (lo que yo no puedo hacer)

Anota el modelo exacto (Ajustes → Acerca del teléfono) y la versión de Android. En cada prueba, abre Ajustes (⚙)
en la app y copia la línea de «Motor de audio» (API, Hz, burst, latencias, XRuns).

| # | Prueba | Cómo | Qué anotar |
|---|---|---|---|
| 1 | Grabación básica | Crea proyecto, arma Pista 1 (R), graba 30 s de voz, detén, reproduce | ¿Se oye bien? ¿Clics o cortes? |
| 2 | 4 pistas sincronizadas | Metrónomo a 100 BPM, graba palmas en 4 pistas siguiendo el clic | ¿Las palmas caen juntas al reproducir? |
| 3 | Latencia de grabación | Con auriculares con cable: graba palmas sobre el clic 8 compases. Haz zoom y mide cuánto se adelanta/atrasa la palma respecto a la línea del tiempo | ms de desfase. Ajusta «Compensación de grabación» hasta que coincida y anota el valor |
| 4 | Bluetooth | Repite la 3 con auriculares Bluetooth | ms de desfase (esperado mucho mayor) |
| 5 | Monitorización | Activa monitorización con auriculares con cable y habla | ¿Retardo perceptible? (sí/no, molesta/no) |
| 6 | Interrupción | Durante una grabación, haz que te llamen | ¿Se detuvo? ¿Se guardó la toma? |
| 7 | Desconexión | Reproduciendo, desconecta los auriculares | ¿Se detuvo sin sonar por el altavoz? ¿Vuelve a sonar al pulsar play? |
| 8 | Cierre forzado | Grabando, fuerza el cierre (Ajustes → Apps → CAUCESOUND → Forzar detención). Reabre el proyecto | ¿Aviso de grabación recuperada? ¿Cuántos segundos se recuperaron? |
| 9 | Exportación | Exporta a 24 bits, ábrelo en FL Studio o Audacity en Windows | ¿Suena igual que en la app? ¿Duración correcta? |
| 10 | Carga | 8 pistas con audio importado, reproduce 5 min | XRuns, temperatura, ¿cortes? |
| 11 | Importación | Importa un MP3, un M4A y un WAV de 44,1 kHz | ¿Todos importan y suenan a la velocidad correcta? |

Envíame los resultados tal cual (también los fallos): con ellos se calibran la compensación por defecto y las
prioridades de la fase 2.
