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
| Robolectric | 4.16.1 | MIT | Solo pruebas de interfaz en la JVM | No |
| Roborazzi (+ compose) | 1.70.0 | Apache 2.0 | Solo pruebas: capturas renderizadas en la JVM | No |
| Compose UI Test (junit4, manifest) | BOM 2025.12.01 | Apache 2.0 | Solo pruebas (el manifest de prueba va solo en `debug`) | No |

No se incluye JUCE, ni samples, fuentes, imágenes o modelos de IA de terceros.

## Contenido musical de la fase 1.2

| Elemento | Origen | Derechos |
|---|---|---|
| Sonidos de Cauce Drums (4 kits, 8 carriles) | **Síntesis propia** en `core/src/Drums.cpp` (osciladores, ruido, envolventes), generados al cargar el kit | Propios del proyecto. Ningún sample grabado ni descargado |
| Cauce Synth y sus 7 presets | Síntesis sustractiva propia (`core/src/Synth.cpp`) | Propios. Los nombres describen el carácter («808 Bass» es un nombre genérico de estilo, no reproduce grabaciones de ninguna caja de ritmos) |
| Cauce FX (filtro, EQ, saturación, compresor, delay, reverb) | Implementación propia (`core/src/Effects.cpp`), algoritmos clásicos de dominio público (SVF TPT, biquads RBJ, red de combs/all-pass) | Propios. Sin código de terceros |
| Patrones y progresiones de las plantillas | Patrones rítmicos y progresiones de 4 acordes genéricos escritos para el proyecto (`core/src/SessionMusic.cpp`) | Ritmos y progresiones genéricas no protegibles; no citan ninguna obra |

### Añadido en la misión 0.4 («Cauce Signature Sound»)

| Elemento | Origen | Derechos |
|---|---|---|
| Kits 4-7: Cauce Dembow, Cauce Trap, Cauce Boom Bap, Cauce Pop (incluye güira, timbal, conga, chasquido, crash, shaker) | **Síntesis propia** en `core/src/Drums.cpp` (capas de bombo, membranas, ruido filtrado, metales inarmónicos, sala corta) | Propios. Sigue sin haber ningún sample grabado ni descargado |
| 10 presets nuevos de Cauce Synth (cauce_808, dembow_bass, cauce_sub, urban_pluck, warm_keys, dark_bell, air_pad, bright_pluck, bright_lead, electro_arp) y los parámetros noiseMix, noiseDecayMs y width (ancho estéreo con chorus) | Síntesis propia (`core/src/Synth.cpp`) | Propios |
| Arreglos de 16 compases, patrones por sección, bajos, acordes y melodías | Compositor local por reglas y semilla (`core/src/Composer.cpp`): progresiones diatónicas genéricas, ritmos de género (dembow, trap, boom bap, four-on-the-floor) | Recursos de estilo genéricos, no protegibles; ninguna melodía ni patrón copiado de una obra |

Presupuesto usado: 0 $. Ningún banco de sonidos, modelo ni servicio externo.

Si en el futuro se añaden samples grabados, cada paquete necesitará su licencia documentada aquí (origen, autor,
licencia que permita redistribuir dentro de una app comercial) antes de incluirlo.

## Clave de firma de prueba

`caucesound-preview.jks` (RSA 3072, válida 25 años, SHA-256 del certificado `56:B4:DE:46:…:AB:2E`) firma solo los
APK de prueba «Preview». No está en git: vive en la carpeta del proyecto junto a `keystore.properties`. **No sirve
para publicar**: para Google Play hay que crear una clave de subida propia y usar Play App Signing (decisión
pendiente del propietario).

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
