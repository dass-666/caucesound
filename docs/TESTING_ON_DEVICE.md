# Ejecutar CAUCESOUND en un Samsung desde Windows

## Opción rápida: instalar el APK ya compilado (sin Android Studio)

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
3. Conecta el teléfono y pulsa *Run*. O en terminal: `cd android` y `.\gradlew.bat installDebug`.
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
