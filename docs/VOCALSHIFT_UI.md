# VocalShift en CAUCESOUND: interfaz «Dark Rose / Neon Violet» (0.8.1)

Rediseño **solo de interfaz** de la pantalla de VocalShift a partir de `PROMPT_CAUCE_ZONE.md`, el SVG
`cantante_holografica.svg` y las capturas de escritorio `referencia_facil.png` / `referencia_pro.png` que envió Diego.
No cambia el DSP, el adaptador del motor (`core/src/VocalBridge.cpp`), el formato de proyecto ni el submódulo.

## 1. Informe previo (stack y parámetros reales)

| Pregunta | Respuesta comprobada en el código |
|---|---|
| Stack | Android, Kotlin + **Jetpack Compose** (Material 3). Motor propio C++17 (`core/`), audio con **Oboe**, puente JNI. Sin JUCE. |
| ¿Existe VocalShift? | Sí, como submódulo `third_party/vocalshift-ai-studio@10e679c` (rama `feat/urban-spatial`, v0.4.0 del plugin). Se usa **offline**: procesa un clip y deja dos versiones (A original / B procesada). El VST3 no se ejecuta en el móvil; solo su núcleo C++ (`vs::VocalChain`). |
| Archivos | UI: `android/.../ui/VocalShiftPanel.kt` · datos: `data/Plugins.kt` (`VocalParams`) · adaptador: `core/src/VocalBridge.cpp`, `core/include/cs/VocalBridge.h` · sesión: `core/src/SessionVocal.cpp`. |
| Nivel de audio hacia la UI | `NativeEngine.meters(n)` devuelve en cada fotograma el **pico** del master y de cada pista (atómicos del motor, `Engine.cpp`). No hay RMS ni medidores por efecto. |

### Parámetros: qué está conectado y qué no

La tabla de IDs del prompt corresponde al plugin de escritorio v0.5.0. CAUCESOUND guarda sus propias claves
(`VocalParams` ↔ `cs::vocal::Params`), que **no se han cambiado**.

| ID del plugin | Control en la pantalla | Estado en el móvil |
|---|---|---|
| `semitones` | PITCH (−24…+24, enteros) | Conectado (`semitones`) |
| `fine` | Ajuste fino (−100…+100 ct, Pro) | Conectado: se guarda **sumado** a `semitones` (como ya hacía el adaptador con los presets, `fromVs`). Ej.: 7 st + 30 ct = 7,30 |
| `character`, `magic` | CARÁCTER, MAGIA | Conectados |
| `mode` | Identidad / Radical | Conectado (`radical`) |
| `route` | Transformar / Segunda voz | Conectado (`secondVoice`) |
| `secondGain` | Fader 2.ª voz (−60 = −∞ … +6 dB) | Conectado (`secondGainDb`). Antes el control llegaba a −30; ahora cubre el rango completo que ya aceptaba el motor. Doble toque = valor del preset |
| `formantShift`, `brightness` | Formantes, Brillo (Pro) | Conectados |
| `mix` | Mezcla (Pro, solo Transformar) | Conectado (`mixPercent`) |
| `pan`, `offsetMs` | Pan 2.ª voz, Desfase (Pro, solo Segunda voz) | Conectados |
| `spatial` | Espacio (2.ª voz) | Conectado |
| `bypass` | Bypass | **No es un parámetro del motor móvil.** Se usa para escuchar A (original): solo activo si el clip ya tiene versión procesada |
| `autoLevel`, `safetyClip`, `improved` | Interruptores | **Fijos en `on`** por el adaptador (`toVs`): se muestran encendidos y deshabilitados |
| `spaceWidth`, `spaceReverb`, `spaceDelay` | Anchura, Reverb, Eco corto | **Fijos en 100 %** (el adaptador no los pasa): deshabilitados |
| `outputGain` | Ganancia de salida | **Fija en 0 dB**: deshabilitada |
| `engine` (PSOLA) | Motor experimental | **No existe** en la versión fijada del submódulo: deshabilitado |

Los parámetros fijos existen en el núcleo `vs::VoiceParams` del submódulo. Exponerlos solo exige pasarlos en
`VocalBridge.cpp` y añadirlos a `VocalParams` (sin tocar el DSP; con los valores actuales el resultado sería
idéntico bit a bit). No se ha hecho porque la orden era «solo interfaz».

Presets: los 12 de fábrica que trae el submódulo fijado (Urbano Espacial, Referencia, Segunda voz +7, Segunda voz
+12, Neutro, Agudo natural/intenso, Grave natural/profundo, Voz brillante/oscura, Efecto creativo extremo). Los que
cita el prompt y no están (Rap Melódico, Pop Brillante, Dark Vocal, Rock Texturizado, Dream Atmosphere…) son de una
versión posterior del plugin.

## 2. Qué se implementó

- `ui/VsControls.kt`: tokens `VS` (paleta exacta del prompt), `VsPanel`, `VsKnob` (arco 270°, degradado violeta →
  rosa con halo, valor dentro, escala −24…+24 en la grande; arrastre vertical, doble toque = defecto, mantener =
  escribir el valor), `VsFader` (vertical u horizontal, escala +6…−∞), `VsSegmented`, `VsSwitch`, `VsMeter` (LED,
  clip rojo que se borra al tocar), `ValueEntryDialog`.
- `ui/SingerIllustration.kt` + `ui/SingerArt.kt` (generado por `android/tools/svg_to_singer_kt.py` desde el SVG):
  la cantante en vectores, fondo del panel, anillos, líneas de escaneo y fundido inferior.
- `ui/VocalShiftPanel.kt`: barra superior (nombre, Bypass, presets en hoja inferior, Fácil | Pro), ilustración con
  medidores y selector de ruta, vista Fácil (PITCH grande con −12/+12, Carácter y Magia a los lados, Identidad |
  Radical con descripción, fader de 2.ª voz horizontal, interruptores, línea «Efectivo») y vista Pro (paneles PITCH,
  TIMBRE, DINÁMICA, ESPACIO, MEZCLA, 2.ª VOZ, CALIDAD Y DIAGNÓSTICO). Debajo sigue el flujo de siempre: forma de onda,
  Ajustar escuchando, Prueba rápida, Procesar y Comparar.
- Horizontal (ancho ≥ 600 dp): dos columnas (ilustración | controles). La distribución de escritorio en tres
  columnas para tableta horizontal no se hizo.

### Medidores

- **OUT** = pico real de la pista del clip (el mismo dato del mezclador). Con A suena el original; con B, el procesado.
- **IN** apagado: VocalShift procesa offline y no hay entrada en directo que medir.
- La línea «Efectivo» muestra los valores que se envían al motor. Los valores internos que calcula el núcleo a partir
  de Carácter y Magia (anchura, RT60, eco…) no salen del adaptador y no se muestran.

## 3. Rendimiento de la ilustración

- Parte estática en un mapa de bits dentro de `drawWithCache`: se rasteriza una vez y solo al cambiar el tamaño.
- Capa reactiva en su propio `graphicsLayer`; el nivel se calcula con la fórmula del prompt y solo se repinta si cambia
  más de 0,003. Bucle a 30 Hz como máximo, solo con el transporte en marcha (o mientras se apaga el brillo). En
  silencio, 0 repintados. Al cerrar la pantalla el bucle se cancela.
- Medido en la JVM (x86, Skia por software en Robolectric, panel 1080×950 px; **no es un teléfono**): ver
  `measure_draw_cost_on_the_jvm` y el informe de la entrega.

**Sin medir en un teléfono**: fps y CPU reales. Protocolo para el Samsung:
`adb shell dumpsys gfxinfo com.caucesound.app.preview reset`, abrir VocalShift con la voz sonando 20 s y
`adb shell dumpsys gfxinfo com.caucesound.app.preview` (mirar «Janky frames» y percentiles 50/90/99).

## 4. Pruebas

`VocalShiftUiTest` (JVM): cada control escribe el mismo campo de `VocalParams` y solo ese; ajuste fino ida y vuelta;
presets desde la hoja; Bypass; fórmula del nivel; silencio sin repintados; nada corriendo al cerrar; caché estática
única; coste de dibujo; capturas 60-66 (renders JVM en español, no capturas de teléfono).

## 5. Pendiente

1. Probar en el Samsung (fluidez con la voz sonando, gestos de las perillas, legibilidad).
2. Decidir si se exponen los 7 parámetros fijos (cambio pequeño en el adaptador, sin DSP).
3. Actualizar el submódulo a la versión del plugin con PSOLA y los presets nuevos, si Diego lo autoriza.
4. Tres columnas en tableta horizontal, si hace falta.
5. Licencia del arte: el SVG lleva credenciales de contenido C2PA que indican que lo produjo Claude a petición de un
   usuario; es arte original de VocalShift sin terceros, pero conviene dejarlo anotado en LICENSES.md antes de publicar.
