# Formato de proyecto v3 (0.7.0)

## Qué cambia

- `project.json` pasa de `"version": 2` a `"version": 3`.
- Única novedad: en las pistas de batería, el campo opcional `"laneRoutes": { "0": "<id de un bus>" }`.
  - Significa que la pieza 0 del kit (p. ej. el bombo) sale por su propio canal del Mixer: un bus normal, con fader, mute/solo, medidor e inserciones.
  - Si el campo falta, la pieza suena dentro de la batería, como en la v2.
- `kMaxBuses` sube de 4 a 8, porque cada pieza con canal propio usa un bus.

## Compatibilidad

| Caso | Qué pasa | Prueba |
|---|---|---|
| Abrir un proyecto v2 | Abre igual, sin `laneRoutes`. Al abrirlo se deja, **una sola vez**, una copia intacta del original en `project.v2.json` dentro de la carpeta del proyecto. El siguiente guardado escribe la v3 | `channel_rack_v2_project_opens_unchanged_and_keeps_its_original` |
| Proyecto v3 en la app 0.6 | **No compatible hacia atrás**: la 0.6 lo rechaza con el mensaje «proyecto creado con una versión más nueva de la app» (comprobado en su código, `Project.cpp`). La copia `project.v2.json` permite volver a la 0.6 a mano | — |
| Validación | Las rutas solo valen en pistas de batería, con piezas 0-7, hacia un bus existente y como máximo una pieza por bus. Un `project.json` que no cumpla esto se rechaza al abrir, sin quedar a medias | `channel_rack_limits_and_invalid_routes_are_rejected` |
| Borrar el bus de una pieza | La ruta se quita y la pieza vuelve a la batería; deshacer lo recupera | `channel_rack_drum_piece_on_own_channel_is_real_routing` |

## Exportación

- **Mezcla y exportación para DAW:** la pieza enrutada suena igual en vivo y exportada (diferencia 0).
- **Stems «secos»:** la pieza queda dentro del stem de la batería.
- **Stems «con efectos»:** el stem del bus de la pieza la contiene con sus inserciones.
- **Prueba nula de los stems:** suma de stems = mezcla, con diferencia 0,00e+00.

## Diferencia de sonido documentada

El *soft-clip* de la batería solo actúa sobre la suma principal. Es un límite suave que empieza a 0,8 de amplitud. Una pieza en su canal no pasa por él.

Con el kit a volumen normal no se nota. Si la batería ya entraba en ese límite, la pieza enrutada puede sonar algo más fuerte que antes.
