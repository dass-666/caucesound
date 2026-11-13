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
