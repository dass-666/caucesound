# Licencias, dependencias y posibles gastos

| Componente | Versión | Licencia | Uso | Se distribuye |
|---|---|---|---|---|
| nlohmann/json | 3.11.3 | MIT (`third_party/nlohmann/LICENSE.MIT`) | JSON del proyecto y de la API | Sí (en la app) |
| Oboe (Google) | 1.10.0 (Maven `com.google.oboe:oboe`) | Apache 2.0 | E/S de audio Android | Sí |
| AndroidX / Jetpack Compose / Material 3 | BOM 2025.12.01 | Apache 2.0 | Interfaz | Sí |
| Kotlin stdlib | 2.2.21 | Apache 2.0 | App | Sí |
| libc++ (NDK) | r27c | Apache 2.0 con excepción LLVM | Runtime C++ | Sí |
| JUnit | 4.13.2 | EPL 1.0 | Solo pruebas | No |
| org.json (pruebas JVM) | 20240303 | Dominio público / JSON License según versión — verificar | Solo pruebas | No |

No se incluye JUCE, ni samples, fuentes, imágenes o modelos de IA de terceros.

## Pendiente de decidir (propietario)

1. **Licencia del código de CAUCESOUND** (el repositorio no declara ninguna; por defecto, todos los derechos reservados).
2. **Licencia de VocalShift y Solo AI** antes de integrarlos: hoy sus repositorios son públicos y sin licencia.
   Sus núcleos dependen de Signalsmith (MIT): compatible con distribución comercial cerrada.
3. **Rubber Band, Elastique u otros motores comerciales**: no incorporados; requieren licencia de pago.
4. **Modelos de IA** (voz, armonía): cada modelo tiene licencia de código, de pesos y de datos; además se requiere
   consentimiento de la persona cuya voz se modela. Nada incorporado.

## Gastos posibles (ninguno realizado)

| Concepto | Coste aproximado | Cuándo |
|---|---|---|
| Google Play Console | 25 USD, pago único | Al publicar en Play |
| Apple Developer Program | 99 USD/año | Para instalar en iPhone fuera de Xcode, TestFlight y App Store |
| Mac para compilar iOS | Hardware propio o servicio en la nube de pago | Fase iOS |
| GitHub Actions | Gratis dentro de la cuota de la cuenta (repositorio privado: minutos limitados al mes); con límite de gasto en 0 no cobra, solo se detiene | Ya configurado en `.github/workflows/ci.yml` |
| Firma de código / certificados | Play App Signing es gratuito | Publicación |
