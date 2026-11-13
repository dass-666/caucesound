# Taxonomía de sonidos, estilos y nombres (misión 0.9, fase I)

## Principios

- **Nombres originales** con el prefijo «Cauce» (Cauce Raw Boom Bap, Cauce 808 Lab…). Nunca nombres de artistas,
  productores, discos o marcas, ni «al estilo de X». Los estilos son géneros musicales, no personas.
- **Ninguna afiliación**: los textos describen el carácter del sonido («seco», «oscuro», «con pegada»), no a quién se
  parece.
- **No rellenar por rellenar**: una categoría aparece en el explorador solo si hay al menos un instrumento instalado.
  Hoy: Baterías y Bajos (y Teclados si se importa el piano).

## Familias (campo `family` del manifiesto)

| Familia | Qué incluye | Estado 0.9 |
|---|---|---|
| drums | Kits completos de 8 carriles | 3 kits |
| bass | 808, subgraves, bajo eléctrico, contrabajo | 808 Lab, Deep Sub, Electric Bass |
| keys | Piano, Rhodes/EP, órgano | Grand Piano (opcional) |
| strings | Cuerdas, pizzicato | Pendiente (VSCO 2 CE, CC0) |
| guitar | Acústica, eléctrica limpia | Pendiente (sin licencia clara para acústica) |
| percussion | Percusión suelta (shakers, congas) | Dentro de los kits |
| melodic | Leads, plucks, campanas, pads | Cauce Synth (síntesis) |

Las 13 categorías de la misión se tomaron como **dirección**, no como lista a completar en esta versión.

## Estilos (campo `genres`) y filtros

Identificadores estables (los usa el compositor y los filtros): `trap`, `drill`, `reggaeton`, `latin`, `hiphop`,
`boombap`, `jazzrap`, `darkhiphop`, `rnb`, `pop`, `electronic`, `funk`, `ballad`, `lofi`, `afro`. La interfaz muestra un
nombre legible (Boom Bap, Dark Hip Hop, Reggaetón…).

## Etiquetas (campo `tags`)

Libres, cortas, en inglés (se muestran tal cual y sirven para buscar): carácter (`Dark`, `Warm`, `Dusty`, `Punchy`,
`Clean`), tipo (`808`, `Sub`, `Electric`, `Acoustic`), uso (`Modern`, `Organic`). La búsqueda no distingue mayúsculas
ni acentos y también mira nombre, paquete, descripción y estilos.

## Origen (campo `origin`)

`original` (síntesis CAUCESOUND), `cc0` (grabación real de dominio público) o combinaciones (`original + cc0`). El
explorador lo muestra en cada sonido («Original Cauce», «Grabación real CC0», «Cauce + CC0»).
