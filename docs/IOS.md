# Pendiente para iPhone / iPad

Estado: **nada de iOS está implementado**. Este entorno es Linux: no hay Xcode ni macOS, así que no se puede
compilar ni firmar una app de iOS desde aquí.

## Lo que ya sirve para iOS

- `core/` completo (motor, sesión, proyecto, WAV, remuestreo, API de comandos) es C++17 portable, sin APIs de
  Android. Usa `std::filesystem` (iOS 13+) y POSIX `fsync`/`rename`, disponibles en iOS.
- El contrato UI↔motor (`cs::handleCommand` + posición/medidores/picos) es el mismo que usará la app de iOS.
- El formato de proyecto es idéntico: un proyecto creado en Android se podrá abrir en iOS.

## Trabajo pendiente

1. **E/S de audio**: `AudioIO` para iOS con `AVAudioSession` (categoría `.playAndRecord`, modo `.measurement` o
   `.default`, `setPreferredIOBufferDuration` ~5 ms, `setPreferredSampleRate(48000)`) y una unidad
   `kAudioUnitSubType_RemoteIO` cuyo callback llame a `Engine::process`. Gestionar `AVAudioSession.interruptionNotification`
   (llamadas) y `routeChangeNotification` (auriculares), equivalentes a `AudioGuard` en Android.
2. **Puente Swift ↔ C++**: Swift 5.9+ admite interoperabilidad directa con C++; alternativa: una fachada
   Objective-C++ mínima con las mismas funciones que `JniBridge.cpp`.
3. **Interfaz**: decisión pendiente entre
   - **SwiftUI nativo** (mejor integración, dos interfaces que mantener), o
   - **Compose Multiplatform** (comparte casi toda la UI Kotlin actual; iOS estable desde 2025 según JetBrains —
     verificar el estado del timeline/canvas en iOS antes de decidir).
   Recomendación provisional: prototipar la pantalla de timeline con Compose Multiplatform en iOS durante 2–3 días y
   decidir con datos (rendimiento de scroll y gestos).
4. **Importación**: `AVAudioFile`/`ExtAudioFile` para MP3/AAC/ALAC/FLAC → WAV temporal (como `AudioDecoder.kt`).
5. **Archivos**: proyectos en `Application Support`; exportación vía `UIDocumentPickerViewController` / `ShareLink`.
6. **AUv3**: host de AUv3 de terceros (`AVAudioUnitComponentManager`, `AVAudioUnit.instantiate`) envuelto como
   `cs::AudioProcessor`/`cs::Instrument`; más adelante, exportar nuestros motores como extensiones AUv3.
7. **Distribución**: cuenta Apple Developer (**99 USD/año, requiere tu autorización**), un Mac con Xcode (propio o
   un servicio de compilación en la nube, también con coste), TestFlight para pruebas.
8. Validación física en un iPhone: el mismo protocolo de TESTING_ON_DEVICE.md.
