# Formato de paquete Cauce (versión de formato 1) y proyecto v4

Un **paquete Cauce** es una carpeta (o un ZIP que contiene una sola carpeta) con sonidos versionados y portables. Es
independiente de Android: lo leen el motor, las pruebas, la herramienta `cauce_packs` y, en el futuro, otros
reproductores (por ejemplo un DAW web), porque los mapas son **SFZ** (formato abierto) y las muestras **WAV**.

```
cauce.signature.drums/
  cauce-pack.json        manifiesto (obligatorio)
  LICENSE.txt            texto de la licencia (obligatorio)
  PROVENANCE.md          procedencia de cada archivo (recomendado; los de CAUCESOUND lo traen siempre)
  instruments/*.sfz      mapas de muestras
  samples/**.wav         muestras PCM
```

## Manifiesto `cauce-pack.json`

```json
{
  "format": "cauce-pack", "formatVersion": 1,
  "id": "cauce.signature.drums", "version": "1.0.0",
  "name": "Cauce Signature Drums", "author": "CAUCESOUND", "description": "…",
  "license": "LicenseRef-CauceSound-Content AND CC0-1.0", "licenseFile": "LICENSE.txt",
  "minEngine": "0.9.0",
  "sources": [ { "name": "VCSL", "author": "Versilian Studios LLC", "url": "https://github.com/sgossner/VCSL",
                 "version": "<commit>", "license": "CC0-1.0", "retrieved": "2026-11-13", "notes": "…" } ],
  "instruments": [ { "id": "raw-boombap", "name": "Cauce Raw Boom Bap", "family": "drums", "kind": "drumkit",
                     "sfz": "instruments/raw-boombap.sfz", "description": "…", "origin": "original",
                     "tags": ["Boom Bap", "Dusty"], "genres": ["boombap", "jazzrap"],
                     "laneNames": ["Kick", "Snare", "…8 nombres…"], "polyphony": 24, "mono": false,
                     "glideSeconds": 0, "volumeDb": 0, "previewKey": 36, "lowKey": 36, "highKey": 43 } ],
  "files": [ { "path": "samples/kick/k1_v1.wav", "bytes": 38444, "sha256": "…" } ]
}
```

Reglas que comprueba el motor (`parsePackManifest`, `PackLibrary::importZip`):

- `id` e ids de instrumento: `[a-z0-9._-]`; `version`: `x.y.z`. Un instrumento se identifica como `"<id>:<instrumento>"`.
- Rutas relativas seguras (sin `..`, sin absolutas, sin `:` ni `\`). Extensiones permitidas: `.wav .sfz .json .txt .md`.
- `files` debe listar **exactamente** los archivos del paquete con su tamaño y SHA-256; el SFZ de cada instrumento y
  `licenseFile` deben estar listados.
- `kind`: `drumkit` (notas 36-43 = los 8 carriles del Channel Rack, en ese orden) o `melodic`.

## Importación segura de ZIP (sin tienda ni servidor)

El usuario elige un `.zip` con el selector de archivos del sistema. Antes de escribir nada en la biblioteca:

1. Se recorre el directorio central: se rechazan entradas cifradas, enlaces simbólicos, rutas peligrosas (zip-slip),
   más de 5 000 entradas, más de 200 MB por archivo o 1 GB en total, y una relación de compresión > 200 (bombas ZIP).
2. Se extrae en una carpeta temporal `.import-<aleatorio>` dentro de la carpeta de usuario.
3. Se valida el manifiesto, la lista exacta de archivos y **todos los SHA-256**, y se analiza cada SFZ.
4. Solo entonces se instala con un renombrado (atómico en el mismo sistema de archivos). Si ya existe: no se permite
   bajar de versión; reemplazar la misma versión o una anterior requiere `replace`; los paquetes de fábrica no se
   sustituyen por uno importado con la misma versión.

Cualquier fallo deja la biblioteca como estaba (probado: zip-slip, archivo no declarado, `.so`, hash alterado, ZIP
truncado).

**Borrar** solo es posible para paquetes importados. La app pregunta antes y dice qué proyectos guardados lo usan
(`packUsage`); el proyecto abierto no puede perder su paquete. Los proyectos que lo usaban se abren con esas pistas
mudas y sus datos intactos.

## Proyecto v4

- v3 sigue siendo el formato por defecto. Un proyecto se guarda como **v4 solo** si tiene una pista `cs.sampler` o una
  batería con `sampleKit`. Si no, el archivo es idéntico al de la 0.8.
- La primera vez que un proyecto pasa a v4 se guarda `project.v3.json` (copia exacta del original).
- La 0.8 no abre proyectos v4 (rechaza versiones futuras con un mensaje, sin dañar nada). La 0.9 abre v1-v4.
- Campos nuevos: en `instrument.state` (texto JSON) de `cs.sampler`: `instrument`, `packVersion` y los ajustes
  (`volumeDb`, `transpose`, `release`, `velSens`, `drive`, `tone`); en `cs.drums`: `sampleKit`, `sampleKitVersion`.

## Paquetes de la 0.9

| Paquete | Contenido | Tamaño | Licencia | Dónde |
|---|---|---|---|---|
| `cauce.signature.drums` 1.0.0 | Raw Boom Bap, Dark Trap, Latin Pulse (3 capas de fuerza, 2-4 tomas alternas) | 5,8 MB | Original CAUCESOUND + CC0 (VCSL) | Dentro del APK |
| `cauce.bass.lab` 1.0.0 | 808 Lab (glide, 2 capas), Deep Sub | 1,6 MB | Original CAUCESOUND | Dentro del APK |
| `cauce.bass.electric` 1.0.0 | Bajo eléctrico real con dedos, 3 capas | 8,3 MB | CC0 (Karoryfer) | Dentro del APK |
| `cauce.keys.grand` 1.0.0 | Piano de cola Kawai, 3 capas | 40,5 MB | CC0 (VCSL) | ZIP opcional (importar) |

Se regeneran con `cauce_packs <VCSL> <salida> [drums|bass|piano|ebass] [Black And Blue Basses]`
(`desktop/tools/packbuilder/PackBuilder.cpp`). Las muestras de terceros no están en git.

## Compilaciones reproducibles (0.10)

Hasta la 0.9.2 un APK compilado sin `CAUCE_PACKS` salía sin sonidos de fábrica y sin ningún aviso. Desde la 0.10:

- `packs/factory-packs.lock.json` (en git) registra los paquetes que **debe** llevar un APK preview o release: id,
  versión, licencia, SHA-256 del manifiesto y del ZIP, instrumentos que usan los proyectos guardados y un tope de
  tamaño (`maxTotalBytes`) para que el APK no crezca sin decidirlo.
- `python3 tools/factory_packs.py prepare <carpeta con los ZIP> <salida>` comprueba el SHA-256 de cada ZIP y deja la
  carpeta con la forma exacta que espera Gradle (`<salida>/packs/<id>/`).
- La tarea Gradle `verifyFactoryPacks` se ejecuta antes de `assemblePreview` y `assembleRelease` y **detiene la
  compilación** si falta `CAUCE_PACKS`, falta un paquete o un archivo, un SHA-256 o un tamaño no coinciden, hay un
  archivo no declarado o un paquete sin registrar, falta la licencia o `PROVENANCE.md`, una fuente no declara origen y
  licencia, desaparece un instrumento que los proyectos guardados usan, o se supera el tope de tamaño.
- Tras empaquetar, `verifyPreviewApkPacks` abre el APK y comprueba que cada archivo está dentro con su tamaño.
- La variante debug (pruebas y CI) sí se compila sin paquetes; si se le pasan, se verifican igual.
- La app recibe en `BuildConfig.FACTORY_PACKS` lo que la compilación promete y, si al arrancar no lo encuentra
  completo, lo dice en el explorador de sonidos en lugar de mostrar una biblioteca vacía.

Los ids y las versiones de los paquetes existentes no cambian: los proyectos guardados siguen abriendo igual.

## Uso fuera de Android (por ejemplo, un DAW web)

El paquete no depende de Android: es una carpeta con un manifiesto JSON, mapas SFZ y WAV PCM. El esquema formal del
manifiesto está en `docs/schemas/cauce-pack.schema.json` (JSON Schema 2020-12) para que otro reproductor pueda
validarlo sin leer el código C++. Para un navegador hace falta: leer el manifiesto, verificar los SHA-256 (Web Crypto),
interpretar el subconjunto de SFZ documentado arriba y decodificar WAV. No se ha integrado ningún proyecto externo.
