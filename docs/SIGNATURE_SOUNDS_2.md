# Signature Sounds 2.0 y plantillas «Signature 2» (0.10)

**Nadie ha escuchado todavía nada de esto.** En la nube solo se puede medir. Las medidas dicen que algo cambió y que
no hay recortes; no dicen si suena mejor. La decisión es de oído, con los WAV de `muestras-0.10/` y en el Samsung.

## Sonidos nuevos (tres paquetes, generados por `cauce_packs`, bit a bit reproducibles)

| Paquete | Instrumentos | Origen | Tamaño |
|---|---|---|---|
| `cauce.drums.two` (Cauce Signature Drums 2) | Crate Boom Bap, Midnight Trap, Calle Reggaetón, Organic Hip-Hop, Soul Pocket | Bombos y varias cajas: síntesis original. Cajas, charles, palmas, aros, shakers y platillos reales: VCSL (CC0) en el commit `c1ea7bcc`, procesados (afinación, EQ, saturación, sala) | 203 archivos, 8,9 MB |
| `cauce.bass.two` (Cauce Bass Lab II) | 808 Punch, 808 Short, Warm Sub | Síntesis original | 33 archivos, 3,3 MB |
| `cauce.keys.two` (Cauce Keys II) | Warm Electric Piano, Night Pad, Glass Pluck | Síntesis original | 55 archivos, 7,4 MB |

Cada paquete lleva `LICENSE.txt` y `PROVENANCE.md` (archivo por archivo). No se usó ninguna muestra sin licencia de
redistribución verificada.

Qué se cambió respecto a los kits 1.0 y por qué (defectos **medidos** con `tools/analyze_samples.py`):

| Defecto medido en los sonidos anteriores | Cambio |
|---|---|
| Las capas suaves de las tomas reales quedaban unos 24 dB por debajo de la fuerte: una nota fantasma no se oía | Capas escalonadas 5 dB: un golpe suave sigue sonando, con otro timbre |
| Las tomas reales empezaban tarde (silencio antes del ataque) | Se recortan al ataque |
| Los bombos tenían poca energía por encima de 150 Hz (2-6 % entre 150 y 400 Hz): en un altavoz pequeño casi desaparecen | Capa de «golpe» en medios: 3-11 % |
| Bombos y sub con componente continua | Paso alto a 20 Hz |
| El 808 anterior tenía entre el 0,4 y el 0,8 % de su energía por encima de 150 Hz | 808 Punch: 3,8 %, 6,2 % y 10,3 % según la fuerza (capa aditiva de armónicos entre 150 y 900 Hz) |
| Todos los géneros compartían carácter | Cinco kits con piezas, afinación y procesado propios; tres capas de fuerza y varias tomas alternas por pieza |

**No hecho** (y por qué): guitarra acústica (no hay una fuente con licencia verificada a la que este entorno pueda
acceder); el Cauce Electric Bass no se rehízo (su fuente original no está disponible aquí; se usa el 1.0, CC0); el piano
acústico sigue siendo el paquete opcional de 42 MB de la 0.9.

## Plantillas «Signature 2»: un modo opcional

En la pantalla de inicio hay un interruptor **Signature 2 (en pruebas)**, apagado por defecto. Apagado, cada estilo
crea la plantilla de siempre: se comprobó que su audio es idéntico muestra a muestra al de la 0.8 y una prueba congela
sus notas. Los proyectos existentes no cambian.

Encendido, el mismo estilo se crea así:

| Aspecto | Plantilla de siempre | Signature 2 |
|---|---|---|
| Forma | Intro, A, B, Break, Final, Cierre (17 compases) | Intro, Verso, Coro, Verso 2, Coro 2, Cierre (37 compases, frases de 8) |
| Batería | Patrones por estilo | Los mismos golpes principales, con acentos de charles propios del género, notas fantasma de caja en los estilos de hip-hop, dinámica del bombo y redoble de charles en trap; variación dentro del verso y remate al final de cada sección |
| Bajo | Como estaba | Las notas a un paso de un bombo se mueven a él y las que coinciden se acentúan |
| Melodía | Igual en todas las secciones | En el verso deja hueco para la voz (menos notas y más suave); el coro va completo |
| Humanización | Ninguna | Pequeñas diferencias de colocación (hasta 14 ticks, unos 10 ms) y de fuerza. Se regula en Variaciones: Cuadrado, Natural, Suelto. Es determinista y se deshace |
| Sonidos | Sintetizadores y kits sintetizados | Los paquetes 2.0 si están instalados; si falta alguno, la canción se crea igual con el sintetizador y la app lo avisa |

Sonidos por estilo: Trap y Dark Hip-hop usan Midnight Trap, 808 Punch y Night Pad. Reggaetón, Calle Reggaetón, 808
Short y Night Pad. Hip-hop, Organic Hip-Hop, Warm Sub y Warm Electric Piano. Boom Bap, Crate Boom Bap, Cauce Electric
Bass y Warm Electric Piano. Jazz Rap, Soul Pocket, Electric Bass y Warm Electric Piano. Pop, Organic Hip-Hop, Electric
Bass, Glass Pluck y Night Pad. Electrónica conserva su kit sintetizado y cambia a Glass Pluck y Night Pad.

Variar, Regenerar, los candados, Solo AI y AutoMix funcionan igual en este modo (el estilo del proyecto es el mismo).

### Lo que se midió

- **Balance**: cada papel a solas, con sintetizador y con muestras, sobre la misma composición. Los teclados y el
  colchón de muestras salían entre 14 y 21 dB más fuertes que los sintetizadores a igual fader, y tres estilos
  recortaban. Tras el ajuste, cada papel queda a ±0,2 dB del nivel que tenía con el sintetizador, es decir, se conserva
  el balance de la 0.6, y ninguna mezcla recorta (picos entre −5,9 y −3,0 dBFS).
- **Altavoz pequeño** (energía por encima de 150 Hz respecto al total; es una aproximación, no una prueba en un
  teléfono): Trap pasa de −6,3 a −3,8 dB, Reggaetón de −6,9 a −4,1 dB y Dark Hip-hop de −7,3 a −3,5 dB. En Hip-hop,
  Boom Bap, Jazz Rap, Pop y Electrónica **no hay mejora medible** (entre −0,3 y +0,2 dB).
- **Bajo con el bombo**: en Reggaetón las notas de bajo que caen con un bombo pasan del 33 % al 67 %. En Trap, Dark,
  Hip-hop y Boom Bap **no cambia nada** (ya coincidían entre el 50 y el 79 %).
- **Sonoridad**: con igual RMS por papel, los sonidos 2.0 de Trap y Dark se perciben entre 1 y 2,5 LU más fuertes
  (tienen más medios). Los WAV comparativos están igualados para que eso no decida la comparación.

Limitación conocida: si se rehace solo la batería, el bajo no se recoloca hasta que se rehace el bajo.

### Consumo

- Paquetes de fábrica: de 16,4 MB (tres) a 37,0 MB (seis). Presupuesto fijado en el bloqueo: 40 MiB.
- Memoria: los 17 instrumentos de los seis paquetes cargados a la vez ocupan 35,1 MB de PCM (16 bits). Una canción
  Signature 2 carga un kit y dos o tres instrumentos, no todos.
- CPU del sampler: sin cambios en el motor; no se ha medido en ARM.
