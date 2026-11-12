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
