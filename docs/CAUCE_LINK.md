# Cauce Link: puente entre el teléfono y el ordenador (diseño, misión 0.6)

**Estado: documento de diseño.** No hay código de sincronización en el repositorio. Lo único que ya existe y
funciona es la primera etapa («archivos locales»): el proyecto `.caucesound` y el paquete `-cauce-daw.zip` que el
teléfono exporta y que el programa `cauce` (carpeta `desktop/cli`) abre, verifica y vuelve a renderizar en un
ordenador. Todo lo demás de este documento es propuesta, no hecho.

## 1. Qué problema resuelve

Empezar una canción en el teléfono y terminarla en un ordenador (en CauceSound Studio o en otro DAW) sin perder
trabajo y sin que una copia pise a la otra. Hay tres caminos, de menos a más ambicioso:

| Etapa | Qué es | Estado | Riesgo |
|---|---|---|---|
| 1. Archivos locales | Exportar `.caucesound` o el paquete para DAW y moverlo por cable, Drive o correo | **Hecho** (0.5 y 0.6) | Bajo |
| 2. Wi-Fi local | El teléfono y el ordenador se emparejan y se pasan proyectos por la red de casa | Diseñado aquí | Medio |
| 3. Plugin VST3 | Un plugin dentro de FL Studio, Ableton, etc. que recibe los stems | Solo viabilidad (§9) | Alto |

Recomendación: no empezar la etapa 2 hasta que Diego haya usado la etapa 1 de verdad (experiencia 5 de
`SAMSUNG_ACCEPTANCE_0.6.md`). Si mover un ZIP a mano resulta suficiente, la etapa 2 es un gasto que no hace falta.

## 2. Piezas

```
Teléfono (CauceSound Mobile)                 Ordenador (compañero «Cauce Link» para Windows)
┌──────────────────────────┐                 ┌───────────────────────────────────────────┐
│ motor cs_core            │   Wi-Fi local   │ motor cs_core (el mismo código)           │
│ biblioteca de proyectos  │◄───────────────►│ carpeta «CauceSound» en Documentos        │
│ cliente Link (Kotlin)    │  TLS + clave    │ servicio Link (C++) + icono en la bandeja │
└──────────────────────────┘  de emparejado  └───────────────────────────────────────────┘
```

- **Compañero de Windows**: un programa pequeño sin interfaz de edición. Guarda los proyectos recibidos en una
  carpeta normal, muestra el historial de versiones y abre el paquete para DAW en el Explorador. Se construye sobre
  `desktop/cli` (ya compila el motor en escritorio; ver CAUCESOUND_STUDIO.md).
- **Sin servidor en Internet.** Los dos equipos hablan entre sí dentro de la misma red. Cumple la restricción de no
  enviar grabaciones a servidores externos y cuesta 0 $. Consecuencia que hay que aceptar: fuera de casa no sincroniza.

## 3. Descubrimiento y comunicación local

- Descubrimiento por mDNS/DNS-SD (servicio `_caucelink._tcp`): Android lo trae de serie (`NsdManager`) y Windows 10+
  también. Alternativa si la red lo bloquea: escribir la dirección IP a mano o escanear un código QR que la contiene.
- Transporte: HTTPS sobre TCP en la red local, con un puerto elegido por el compañero. Se usa HTTP normal (peticiones
  `GET`/`PUT` con rangos) porque permite reanudar transferencias cortadas sin inventar un protocolo.
- Android exige permiso de red local a partir de Android 13 para el descubrimiento; se pide solo al abrir Cauce Link.

## 4. Emparejamiento seguro

Objetivo: que nadie más en la misma Wi-Fi (un bar, un estudio compartido) pueda leer ni enviar proyectos.

1. El compañero de Windows genera un par de claves y muestra un **código QR** con su huella (SHA-256 del certificado)
   y un secreto de un solo uso de 128 bits. También lo muestra como 8 palabras para escribirlo a mano.
2. El teléfono escanea el QR, se conecta y comprueba que el certificado del ordenador coincide con la huella
   (esto se llama «fijación de certificado»: evita que un tercero se haga pasar por el ordenador).
3. Los dos derivan una clave de sesión a partir del secreto (intercambio autenticado, p. ej. SPAKE2 o el propio
   secreto como clave precompartida de TLS). El teléfono envía su propia clave pública, que el ordenador guarda.
4. Desde entonces cada conexión es TLS con autenticación mutua. El secreto del QR deja de valer tras un uso o 2 minutos.
5. Cualquiera de los dos lados puede «olvidar» al otro; eso borra las claves guardadas.

Las claves se guardan en Android Keystore y en DPAPI de Windows (almacenes del sistema: no quedan en un archivo legible).
Límite honesto: esto protege la red, no un ordenador ya comprometido.

## 5. Identidad y versiones de un proyecto

Hoy `ProjectModel` tiene `id`, `createdAt` y `modifiedAt`. **No basta** para sincronizar: la hora de modificación no
dice si dos copias vienen una de otra o si se editaron por separado, y los relojes de dos equipos no coinciden.

Cambio de formato necesario (versión siguiente del formato, compatible hacia atrás porque son campos nuevos):

| Campo | Para qué |
|---|---|
| `revision` | Identificador aleatorio que cambia en cada guardado |
| `parents` | Revisión o revisiones de las que procede este guardado |
| `device` | Identificador del equipo que hizo el guardado |
| `contentHash` | SHA-256 del `project.json` normalizado |

Con `revision` y `parents` cada equipo sabe, sin mirar el reloj, cuál de estos tres casos tiene delante:

- **Misma revisión**: no hay nada que hacer.
- **Una desciende de la otra** (la revisión local aparece entre los antepasados de la remota, o al revés): se puede
  avanzar sin perder nada. Es el único caso en el que se sobrescribe, y aun así la anterior queda en el historial.
- **Divergencia** (ninguna desciende de la otra): **conflicto**. Ver §7.

**Historial**: cada equipo guarda las últimas N revisiones de `project.json` (pesan kilobytes). Los audios no se
duplican: se guardan una sola vez por su hash (§6), así que conservar historial casi no ocupa.

## 6. Hashes, detección de cambios y transferencia incremental

Un proyecto es un `project.json` pequeño más audios grandes que casi nunca cambian (una grabación no se edita: los
recortes y VocalShift son no destructivos y crean archivos nuevos). Eso hace sencilla la sincronización incremental:

1. Cada lado calcula un **manifiesto**: lista de archivos con tamaño y SHA-256 (la clase `Sha256` y el manifiesto del
   paquete para DAW ya existen y están probados; se reutilizan).
2. Se comparan manifiestos. Solo viajan los archivos cuyo hash no tiene el otro lado. En una sesión normal, eso es el
   `project.json` y las tomas nuevas.
3. Cada archivo se recibe con nombre temporal, se comprueba su hash y solo entonces se renombra. El `project.json`
   se coloca **el último**: si la transferencia se corta, el proyecto anterior sigue íntegro.
4. Las transferencias cortadas se reanudan por rangos. Los hashes se guardan en caché junto con tamaño y fecha del
   archivo para no recalcular cientos de megabytes en cada conexión.

Coste que hay que medir en un teléfono real antes de prometer nada: calcular SHA-256 de 200 MB de audio.

## 7. Conflictos

Regla que no admite excepción: **una sincronización nunca sobrescribe en silencio la versión más reciente de una
canción.** Ante divergencia no se elige ganador automáticamente, ni siquiera «la más nueva», porque la más nueva
puede ser un cambio trivial hecho encima de una versión vieja.

Lo que se hace:

1. Se conservan **las dos**. La remota entra como un proyecto aparte: «Mi canción (del ordenador, 12 nov 19:40)».
2. Se avisa al usuario con las dos fechas, el equipo de cada una y un resumen de diferencias (pistas añadidas o
   quitadas, clips cambiados, tempo). El resumen sale de comparar los dos `project.json`, que tienen identificadores
   estables por pista y por clip.
3. El usuario decide: quedarse con una, con la otra, o con ambas como canciones distintas.

**No se propone fusión automática** de dos ediciones en esta fase. Fusionar bien un arreglo musical (dos personas
moviendo el mismo clip) es un problema difícil y un error ahí destruye trabajo. Hasta que exista un modelo de
conflictos probado con pruebas automáticas, **no hay sincronización bidireccional automática**: el flujo es
«enviar» y «recibir» explícitos, con la comprobación de §5 antes de escribir.

Pruebas que debe tener el modelo antes de activarse (todas automatizables con dos carpetas en un mismo equipo):
avance simple en cada sentido; divergencia detectada y ninguna copia perdida; corte a mitad de transferencia;
archivo corrupto rechazado por hash; reloj del sistema cambiado; mismo proyecto recibido dos veces.

## 8. Sin conexión, importar y exportar

- Todo funciona sin conexión: cada equipo edita su copia y la sincronización solo ocurre cuando ambos se ven.
- **Importar** en el teléfono lo que vuelve del ordenador: un `.caucesound` (ya existe) o audios sueltos como pistas
  (por ejemplo, la mezcla hecha en FL Studio). El paquete para DAW **no** se reimporta como proyecto: lleva audio ya
  renderizado, no la sesión editable; por eso incluye también el `.caucesound` en `/project`.
- **Exportar**: lo que ya hace la 0.6.

## 9. Viabilidad de un plugin VST3 (solo estudio, no se desarrolla)

Idea: un plugin que se abre en FL Studio y muestra las canciones del teléfono para arrastrar stems y MIDI.

- **Técnicamente posible.** El SDK de VST3 de Steinberg tiene desde octubre de 2025 licencia MIT (antes era doble
  GPLv3/propietaria), así que puede usarse en un producto cerrado sin firmar acuerdo. *Dato a confirmar leyendo la
  licencia del SDK en el momento de empezar; no se ha descargado en esta misión.*
- **Qué no puede hacer**: crear pistas en el DAW anfitrión. Ningún formato de plugin lo permite. El plugin solo
  puede ofrecer archivos para arrastrar, o reproducir él mismo el audio sincronizado con el transporte del DAW.
- **Coste real**: interfaz gráfica propia para escritorio, instalador, firma de código en Windows (un certificado
  cuesta dinero cada año) y pruebas en cada DAW. Es un producto entero, no una función.
- **Alternativa barata con el 80 % del valor**: el compañero de Windows deja el paquete en una carpeta y el usuario
  arrastra los WAV al DAW desde el Explorador. Eso ya funciona hoy con el ZIP.

Recomendación: no empezar el VST3 hasta que la etapa 2 exista y se use.

## 10. Orden propuesto y criterio para avanzar

1. (Hecho) Paquete para DAW y `cauce verify` en escritorio.
2. Campos `revision`/`parents` en el formato y prueba de detección de divergencia con dos carpetas. Sin red.
3. Compañero de Windows que solo **recibe** por Wi-Fi tras emparejar. Un solo sentido: no puede haber conflicto.
4. Envío en el otro sentido con la pantalla de conflictos.

Cada paso se queda si Diego lo usa; si no, se para ahí.
