# Aceptación en Samsung de la 0.8.0 (8 pruebas)

APK: `caucesound-0.8.0-studio-flow-preview.apk` (versionCode 10).

- Se instala **encima** de la 0.7.0 Preview: mismo identificador `com.caucesound.app.preview` y misma firma (certificado
  `56b4de46…61ab2e`, comprobado con `apksigner`). **No desinstales** la anterior: los proyectos se conservan.
- Antes de empezar, **copia de seguridad** recomendada: Inicio → menú del proyecto → Exportar.
- Ninguna de estas pruebas se ha hecho en un teléfono. Todo lo de abajo está probado solo en Linux (motor) y en la JVM
  (interfaz).

Anota en cada prueba «bien», «mal» o lo que oíste.

## 1. Play entre proyectos (regresión)

1. Abre un proyecto, pulsa Play, para y vuelve a Inicio.
2. Abre otro proyecto distinto y pulsa Play. Repite 3 veces alternando.

**Esperado:** siempre suena, sin reiniciar la app (ya lo validaste en la 0.6; esta prueba confirma que no se ha roto).

## 2. Piano Roll: mover notas en todas las direcciones

1. Abre una pista de melodía en el Piano Roll.
2. Arrastra una nota hacia la **izquierda**, luego a la derecha, arriba, abajo y en diagonal.
3. Arrástrala por el **borde derecho** para alargarla y acortarla.
4. Cambia la rejilla a «Libre» y muévela un poco. Pulsa deshacer.

**Esperado:**

- La nota va hacia donde la arrastras, también hacia atrás (antes no se movía a la izquierda).
- Al cambiar de tecla suena la nota nueva.
- Mover no cambia la duración, y cambiar la duración no mueve la nota.
- Con «Libre» no se ajusta a la rejilla.
- Deshacer devuelve la nota a su sitio.

Comprueba también si el dedo tapa demasiado la nota o si es difícil agarrar el borde: es lo que no se puede medir aquí.

## 3. Mixer

1. Abre Mixer en vertical.

**Esperado:**

- Faders, medidores, pan, M y S visibles sin desplazar, con los efectos ocultos.
- «Ver efectos» abajo muestra los del canal elegido, con su volumen.
- El botón FX de una tira abre directamente sus efectos.
- Gira a horizontal: todo debe seguir siendo usable.
- Sube un fader hasta saturar: el medidor marca rojo arriba durante un momento.

## 4. Grabar con audífonos (modo estudio)

1. Ajustes (engranaje) → «Grabación de voz» → «Con audífonos».
2. Con audífonos con cable, graba 10 s cantando sobre el beat.
3. Pulsa S en la pista de voz y escucha la toma sola.

**Esperado:** la toma solo tiene tu voz, sin beat. El motor lo garantiza (prueba con error 0). Si oyes beat con
audífonos, avísame: sería fuga por el propio cable o el micrófono, no la mezcla.

## 5. Grabar con bocinas (altavoz)

1. Ajustes → «Con bocinas». Deja «Bajar el beat mientras grabas» en −10 dB.
2. Sin audífonos, graba 10 s. Escucha la toma sola.
3. Repite con −24 dB y con 0 dB.

**Esperado:**

- Mientras grabas, el beat suena más bajo en el altavoz y vuelve a su nivel al parar.
- En la toma queda **algo** de beat (lo capta el micrófono por el aire): menos cuanto más baja el beat.
- Esto **no** elimina el beat por completo; nadie puede prometerlo con altavoz.
- La mezcla y la exportación no cambian.

## 6. Cancelación de eco del teléfono (experimental)

1. Ajustes → «Con bocinas» → activa «Cancelación de eco del teléfono». Graba 10 s y escucha.
2. Compara con la toma de la prueba 5.

**Esperado:** puede quitar más beat, pero también puede apagar o metalizar la voz; depende del Samsung. Esa toma **no**
guarda copia sin procesar: si no te gusta, desactívala y graba otra vez. Anota cuál de las dos prefieres.

## 7. VocalShift: ajustar escuchando

1. Graba o importa una voz, ábrela en VocalShift → «Ajustar escuchando» → Activar.
2. Mientras suena en bucle, cambia un preset o mueve un control varias veces seguidas.
3. Alterna Original / Con VocalShift.

**Esperado:**

- Cada cambio se oye cuando termina de calcularse; mientras, sigue sonando el anterior.
- Los cambios rápidos no se acumulan: se aplica el último.
- Al desactivar, el bucle vuelve a como estaba.
- El clip original no se modifica hasta que pulses Procesar.

**Mide a ojo cuánto tarda** desde que sueltas el control hasta que lo oyes. En el ordenador son 0,24 s para 6 s de
voz; en el teléfono se espera bastante más, y nunca será instantáneo: no es monitorización en vivo del micrófono.

## 8. Beats: estilos nuevos, Variar y Regenerar

1. Inicio → Nueva canción. Deben aparecer **Boom Bap crudo**, **Jazz Rap** y **Hip-hop oscuro** además de los de antes.
2. Crea uno de cada uno y escúchalos.
3. En uno, ✨ Variaciones → «Variar todo», escucha, pulsa «Versión anterior» y «Versión nueva» para comparar.
4. «Regenerar beat», escucha, y compara igual.
5. Bloquea la pista de acordes (candado) y regenera de nuevo.

**Esperado:**

- Variar suena a la misma canción con detalles distintos.
- Regenerar suena a otra idea del mismo estilo, tono y tempo.
- Con los acordes bloqueados, la armonía no cambia.
- Todo sigue siendo editable en el Piano Roll y en la rejilla.
- Compara también con los WAV de `muestras-0.8/` y su LEEME.

Dime en especial:

- si el Hip-hop oscuro se oye demasiado grave o vacío por el altavoz;
- si la batería regenerada de trap, pop y electrónica te parece demasiado parecida a la original.
