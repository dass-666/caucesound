# AutoMix (prototipo 0.9): sugerencias de mezcla por reglas

**No es inteligencia artificial.** Son reglas fijas y medibles. Está en el Mixer (botón ✨ de la barra superior).

## Qué hace

1. Mide cada pista **sola** con un render offline de los primeros 16 s (RMS antes del máster). No cambia el proyecto
   y no interrumpe la reproducción.
2. Toma la batería como referencia (si no hay batería, la pista más fuerte).
3. Compara cada pista con un objetivo **por papel y estilo** (bajo, acordes, melodía, pad, voz).
4. Propone mover el fader hacia el objetivo, como máximo ±6 dB, y nada si la diferencia es menor de 1,5 dB.
5. El usuario ve la lista (pista, motivo, «antes → después») y decide. **Aplicar** solo mueve faders. **Revertir
   AutoMix** devuelve todos los faders anteriores de una vez (la mezcla no entra en Deshacer en ninguna parte de la app).

## De dónde salen los objetivos

De las propias plantillas, cuyo balance (0.6) aprobó Diego. `cs_render --automix` mide cada papel respecto a la
batería en cada plantilla sin tocar; esos números son la tabla de `core/src/SessionAutoMix.cpp`. Consecuencia,
comprobada por una prueba: **en una plantilla sin tocar AutoMix no propone nada**; solo corrige lo que el usuario
desequilibró (prueba: acordes subidos 9 dB → propone bajarlos 6 dB; revertir deja los faders exactos).

| Estilo | Bajo | Acordes | Melodía | Pad |
|---|---|---|---|---|
| trap | -6,1 | (mediana) | -11,6 | -17,3 |
| reggaeton | -7,3 | -17,3 | -16,3 | -20,1 |
| hiphop | -7,0 | -12,0 | -18,8 | (mediana) |
| boombap | -14,1 | -16,9 | -28,1 | (mediana) |
| jazzrap | -3,5 | -1,6 | -7,7 | (mediana) |
| darkhiphop | -4,3 | -15,8 | -17,0 | -20,8 |
| pop | -6,8 | -16,6 | -15,1 | -19,6 |
| electronic | -13,0 | -22,8 | -24,2 | -22,1 |

dB respecto a la batería (RMS). Proyectos sin estilo: medianas (bajo -6,9, acordes -16,6, melodía -16,6, pad -20,1).

## Limitaciones conocidas (honestas)

- **Voz**: objetivo +1 dB sobre la batería por convención, **sin calibrar** con voces reales (no hay grabaciones con
  licencia en el proyecto). Es la regla más débil.
- RMS no es sonoridad percibida (no pondera frecuencias como el oído; un bajo puede medir fuerte y oírse poco en un
  móvil). Siguiente paso razonable: LUFS de corto plazo (ITU-R BS.1770) por pista.
- No toca panoramas, EQ ni compresión. No mira el enmascaramiento entre pistas.
- Solo analiza 16 s.

## Qué haría falta para algo más ambicioso (no hecho, solo evaluado)

| Idea | Valor | Coste | Recomendación |
|---|---|---|---|
| LUFS por pista y por sección | Alto: se acerca a lo que se oye | Bajo (filtro K + ventanas) | Siguiente versión |
| EQ de «hueco» (paso alto en lo que no es bajo/bombo) | Medio | Bajo; ya hay EQ en Cauce FX | Siguiente versión, como sugerencia |
| Detección de enmascaramiento por bandas | Medio | Medio | Después |
| Modelo entrenado (IA real) | Incierto | Alto: datos de mezclas con licencia, evaluación con oyentes | No antes de tener usuarios reales y datos |

## AutoMix 2 (0.10)

Sigue sin ser inteligencia artificial: son filtros, medias y reglas fijas que se ejecutan en el teléfono, sin servicios
de pago. Además de los faders por papel:

| Regla | Qué mide | Qué propone | Límite |
|---|---|---|---|
| Bombo y caja | El carril de caja y el de bombo, cada uno a solas | Volumen del carril de caja para recuperar la relación del estilo | ±4 dB; nada si la diferencia es menor de 2 dB; solo con el kit con el que se calibró (sintetizado en las plantillas de siempre, Drums II en «Signature 2») |
| Sitio para la voz | Mientras la voz suena, cuánto ocupan acordes, colchón y melodía la banda de 300 a 3400 Hz | Bajar hasta 3 dB la pista que más ocupa si queda a menos de 6 dB de la voz | Nunca toca batería ni bajo. El umbral de 6 dB es una convención, **sin calibrar con voces reales** |
| Choque de graves | Pistas que no son bajo ni batería con más de un tercio de su energía por debajo de 150 Hz y cerca de los graves del bajo | Solo un aviso: «prueba el paso alto de su EQ» | AutoMix no toca ecualizadores |
| Recortes | El pico de la mezcla completa **con los cambios propuestos** | Bajar el máster para que el pico quede en −1 dBFS | Pico de muestra, no pico real entre muestras |

- **Respeta el género**: las relaciones objetivo salen de cada plantilla tal cual (tabla de arriba para las plantillas
  de siempre y otra equivalente para «Signature 2», medida con los paquetes 2.0). No iguala volúmenes: en Boom Bap la
  melodía queda 28 dB por debajo de la batería y en Jazz Rap los teclados casi a su altura, y eso se conserva.
- **Reversible y con A/B**: en el Mixer, después de aplicar, aparece «Comparar» (alterna entre la mezcla anterior y la
  de AutoMix sin perder ninguna) y «Revertir» (deja exactamente la anterior).
- **Comprobado por pruebas** (`core/tests/TestAutoMix2.cpp`): las 8 plantillas sin tocar no reciben ningún cambio;
  la caja 8 dB más baja recibe +4 dB; el máster a +9 dB (pico de +5,8 dBFS, 2957 muestras recortadas) baja a +2 dB y
  la exportación queda en −1,2 dBFS sin recortes; con una voz tapada se bajan 3 dB los teclados, y con la voz clara no
  se propone nada.
- **Lo que no hace**: no ecualiza, no comprime, no mueve panoramas y no mide sonoridad percibida (sigue usando RMS).
  Las reglas dicen que la mezcla cumple unas relaciones, no que suene bien: eso se decide escuchando.
