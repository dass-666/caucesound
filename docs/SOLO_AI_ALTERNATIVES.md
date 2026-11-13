# Solo AI: por qué A, B, C y D sonaban iguales (0.6) y qué cambió en la 0.7

Fallo confirmado por Diego en un Samsung con la 0.6.0: las cuatro alternativas se oían iguales.

Leyenda:

- **Hecho**: comprobado con una prueba automática o una medición reproducible.
- **Inferencia**: conclusión razonable a partir de los hechos, sin prueba directa.
- **Sin verificar**: falta comprobarlo, sobre todo en un teléfono.

## 1. Cómo se investigó

La prueba `solo_ai_alternatives_reach_the_track_and_the_audio_as_the_app_selects_them` está en `core/tests/TestSoloAlternatives.cpp`. Hace lo mismo que la app:

1. Envía los mismos comandos JSON que envía `DawViewModel` (`soloProposals` y después `soloMelody` con `proposal` = 0, 1, 2, 3, 0 y `replaceTrackId`).
2. Lee la pista resultante.
3. Renderiza audio en vivo con `Engine::process` en bloques de 192 muestras, como el callback de Oboe.

Por cada alternativa registra:

- semilla, número de propuesta e id de pista;
- notas (altura, inicio, duración, velocidad), patrón rítmico y hash del MIDI;
- el WAV en vivo.

Para que la prueba escriba el informe, se ejecuta así:

```
CS_SOLO_REPORT_DIR=<carpeta> ./build/cs_core_tests solo_ai_alternatives
```

Escribe `alternativas.json`, `alternativa_X.mid` y `alternativa_X_vivo.wav`. Se ejecutó sobre el commit de la 0.6 (`3ea8f90`, en un worktree aparte) y sobre la 0.7. Los dos resultados están en `muestras-0.7/solo-ai-0.6/` y `muestras-0.7/solo-ai-0.7/` de la carpeta del proyecto.

## 2. Las tres hipótesis y lo que se encontró

| Hipótesis | Resultado en la 0.6 | Tipo |
|---|---|---|
| MIDI idéntico (misma semilla, propuesta ignorada, caché) | **Descartada.** Los cuatro hashes de MIDI son distintos. El solapamiento de notas entre pares es 0,02-0,16 (1 = idénticas) | Hecho |
| MIDI distinto pero la reproducción no cambia (pista o clip viejos, interfaz que no envía la elegida) | **Descartada en el motor.** La pista se sustituye y el audio en vivo cambia en cada paso A→B→C→D→A. La función de la app que construye el comando (`SoloFamily.pickCommand`) se prueba en la JVM: envía 0, 1, 2, 3, 0 sobre la misma pista | Hecho (motor y lógica de la app). En el teléfono: **sin verificar** |
| Demasiado parecidas musicalmente | **Confirmada.** Ver los detalles debajo de la tabla | Hecho (medición) |

Detalles de la tercera hipótesis:

- A, B y C empezaban con las mismas notas (mi-mi-la-do).
- Las cuatro estaban en el mismo registro (64-72) y tenían densidades y duraciones parecidas.
- La distancia de envolvente en vivo entre alternativas era de 6,7 a 9,8 dB.
- La melodía de Solo AI quedaba unos −14 dB por debajo de la mezcla, con la melodía de la plantilla sonando a la vez y tapándola.

**Inferencia:** en un teléfono, con la mezcla completa, esas diferencias pequeñas se perciben como «la misma melodía». No hubo error de selección.

## 3. La corrección (solo en el adaptador de CauceSound)

El núcleo original `solo-ai-studio` (submódulo fijado en `fe8445b`) **no se ha modificado**.

En `core/src/SoloProfiles.cpp`, cada alternativa recibe un perfil musical propio que se aplica sobre la propuesta del núcleo. Todos los perfiles conservan la tonalidad y los acordes:

1. Las notas fuera de la escala se llevan a la escala.
2. Las notas largas en tiempo fuerte se llevan a una nota del acorde.

| Alternativa | Qué hace |
|---|---|
| A · Sencilla y pegadiza | Célula de 2 compases (≤6 notas, rejilla de corcheas) repetida; final del núcleo; notas de ≤1 tiempo; registro medio (≈67) |
| B · Emotiva | Notas en tiempo o largas, legato con respiración cada 4 compases, segunda mitad más aguda y compás de clímax, final largo en tónica o nota del acorde, arco de intensidad (≈71) |
| C · Rítmica | Silencio en el último tiempo de cada compás, notas largas partidas en 3+3+2 semicorcheas, anticipaciones, staccato y acentos, registro grave (≈55) |
| D · Experimental | Segunda mitad invertida, saltos de octava, compases impares en tresillos, velocidad irregular, saltos limitados a 15 semitonos, registro agudo (≈72) |

## 4. Resultado medido (hechos)

**Prueba `solo_ai_profiles_have_measurable_identity_and_keep_the_harmony`**

Se probaron 6 peticiones distintas (estribillo, solo, interludio; fácil, emotivo, enérgico).

- **En la escala:** ≥95 % de las notas.
- **Notas del acorde en tiempo fuerte:** ≥80 %.
- **Solapamiento máximo entre dos alternativas:** 0,17. En la 0.6, A, B y C compartían el arranque.
- **Identidad de cada perfil** (ejemplo, caso 0):

| Alternativa | Duración media (tiempos) | Síncopa | Salto medio (semitonos) | Altura media (MIDI) |
|---|---|---|---|---|
| A | 0,83 | 0,00 | 3,3 | 68 |
| B | 2,03 | 0,00 | 2,6 | 68 |
| C | 0,34 | 0,48 | 2,9 | 56 |
| D | 0,81 | 0,54 | 5,3 | 77 |

**Audio en vivo en la 0.7** (distancia media de la envolvente RMS de 10 ms):

| Comparación | Distancia |
|---|---|
| A-B | 16,1 dB |
| A-C | 13,6 dB |
| A-D | 13,9 dB |
| B-C | 9,6 dB |
| B-D | 9,9 dB |
| C-D | 9,9 dB |
| A frente a A, tras pasar por B, C y D | 0,38 dB |

- En la 0.6, la distancia entre alternativas iba de 6,7 a 9,8 dB.
- La diferencia de 0,38 dB entre A y A se debe a la fase libre de los osciladores, no a las notas.

**Prueba `solo_ai_switching_while_playing…`**

- No quedan notas colgadas al cambiar de alternativa con la reproducción en marcha.
- El resultado sobrevive a guardar y reabrir (12 de 12 notas).
- Deshacer devuelve la alternativa anterior.

## 5. Cambios en la pantalla

- **«Escuchar sola»:** activa el solo de la pista de Solo AI.
- **«Silenciar melodía de la plantilla»:** silencia las pistas con papel `melody` del plan. Es un mute normal, que se puede deshacer.
- **Aviso:** si la melodía de la plantilla suena a la vez, la pantalla avisa de que tapa parte de la diferencia.
- **Textos:** describen el carácter de cada alternativa. El texto antiguo decía que «salen del mismo motivo».
- **Nivel:** la pista de Solo AI entra 3 dB por encima de la melodía de la plantilla, con un máximo de 0 dB.

## 6. Hallazgo de los sanitizadores: la misma semilla no da las mismas notas en todas las compilaciones

**Qué se vio (hechos):**

- La compilación con ASan dio notas distintas a la Release en 2 de los 6 casos de la prueba, con la misma semilla. TSan y Release coinciden.
- Valgrind no encontró lecturas de memoria sin inicializar en esa prueba.
- En uno de esos casos, D salía con saltos más cortos que A (3,7 frente a 3,8 semitonos). La identidad de D no estaba garantizada.

**Corrección (hecho):** D añade saltos de octava en notas débiles hasta un salto medio de unos 5 semitonos. Respeta los límites de una décima entre notas vecinas y el registro 57-88. Con eso, la prueba pasa en Release y en ASan; D queda entre 4,8 y 6,0 semitonos en los 6 casos.

**Causa de la diferencia (inferencia, sin verificar):** no se ha encontrado. Lo más probable es una diferencia de redondeo en coma flotante entre compilaciones, dentro del núcleo o del adaptador.

**Consecuencias:**

- **Sin efecto:** los proyectos guardados no cambian, porque guardan las notas, no la semilla.
- **Puede pasar:** la misma petición, con la misma semilla, puede dar otra melodía en el teléfono (arm64) que en Linux. La prueba de igualdad entre plataformas no existe y está sin hacer.

## 7. Lo que no se ha resuelto

- **Detector de tonalidad:** sigue siendo poco fiable. Acertó exactamente 1 de 15 canciones propias y hubo 1 error con confianza 0,68. No se ha cambiado. La app lo presenta como estimación y mantiene la corrección manual. Es un fallo del núcleo original; se documenta, no se toca.
- **Teléfono:** **sin verificar.** La escucha en el teléfono decide si ahora «suenan distintas» lo suficiente. Las mediciones no sustituyen el oído.
