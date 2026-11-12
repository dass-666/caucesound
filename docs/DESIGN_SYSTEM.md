# Sistema de diseño de CAUCESOUND

Código: `android/app/src/main/java/com/caucesound/app/ui/Theme.kt` (tokens) y `Components.kt` (componentes).
Las capturas de referencia son renders JVM (`./gradlew testDebugUnitTest -Pscreenshots`), no fotos de un teléfono.

## Reglas

| Tema | Regla |
|---|---|
| Fondos | Carbón en cuatro niveles: `Background` < `Surface` < `SurfaceHigh` < `SurfaceTop`. Sin sombras: la profundidad es el nivel de superficie más un borde de 1 dp (`Stroke`) |
| Violeta | Acción principal y selección. Un solo botón primario por pantalla |
| Cian | Estado en vivo: reproducción, columna que suena, metrónomo y bucle activos |
| Rojo | Grabar y borrar. Nada más |
| Ámbar | Solo y avisos (incluida la cuenta previa) |
| Color de pista | Identifica la pista en cabecera, clips, tira del mezclador y notas. `kindColor(kind)` da el color por tipo |
| Espaciado | `Space`: 4, 8, 12, 16, 24 dp |
| Radios | `Dims.RadiusLarge` 16 (tarjetas), `RadiusMedium` 12 (botones), `RadiusSmall` 8 (clips, pasos) |
| Zonas táctiles | 48 dp (`Dims.Touch`) por defecto; nunca menos de 44 dp (`TouchMin`), 40 dp solo en el transporte de teléfonos de menos de 400 dp de ancho |
| Tipografía | Sans para etiquetas; monoespaciada solo para números que cambian (`ValueText`) |
| Dibujo | Todo `Canvas` trabaja en dp (`1.dp.toPx()`), nunca en píxeles fijos |
| Poca altura | Con menos de 500 dp de alto (horizontal): transporte dentro de la barra superior, navegación compacta, una fila de herramientas, tiras de mezclador anchas |

## Componentes

| Componente | Uso |
|---|---|
| `Knob` | Perilla: arrastre vertical u horizontal (180 dp = recorrido completo), doble toque = valor por defecto, `log` para frecuencias y tiempos |
| `VerticalFader` | Fader en dB (-60 a +6) con marcas en 0, -12, -24 y -48 |
| `LevelMeter` | Medidor de pico, rojo desde -3 dB; se repinta sin recomponer |
| `DragValue` | Estado común de perillas y faders: el dedo manda mientras arrastra; el motor manda en cuanto su valor cambia (deshacer, preset) |
| `CauceButton` | Botón de texto: `Primary`, `Secondary`, `Ghost`, `Danger` |
| `ToolButton` | Botón de icono de barra de herramientas, con estado activo |
| `PillToggle`, `Segmented` | Conmutadores y selectores de opción |
| `PanelHeader` | Cabecera de cada panel: título, subtítulo, acciones |
| `EmptyState` | Estado vacío o de error con acción |
| `CauceDialog` | Diálogo con los botones del sistema |
| `SectionCard` | Tarjeta de sección con título |

## Al añadir una pantalla

1. Usa los componentes anteriores; no crees botones ni perillas nuevos.
2. Añade una prueba en `UiScreensTest` con `shot("NN_nombre")` y revisa la imagen en vertical, 360 dp y horizontal.
3. Todo control que cambie un valor del motor debe leer el valor del estado del motor (no guardar una copia propia).
