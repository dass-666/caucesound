# CAUCESOUND: interoperabilidad futura (misión 0.9, fase H)

Objetivo: que lo construido en la 0.9 (sampler, paquetes de sonidos, proyectos v4) pueda compartirse más adelante con
el DAW web de Ronaldo **sin decidir nada por él**. No se ha accedido a sus repositorios ni servidores, no se ha
interrumpido a nadie y no hay ningún acuerdo: todo lo de abajo son propuestas y preguntas.

Relacionado: `/mnt/project-files/caucesound/union-web/RESUMEN_TECNICO_CAUCESOUND.md` (plan de unión del 13-11).

## 1. Qué ya está preparado (hecho en la 0.9, comprobado)

| Pieza | Por qué sirve a una web | Evidencia |
|---|---|---|
| Paquetes = carpeta con **SFZ + WAV + manifiesto JSON** | SFZ y WAV son formatos abiertos; cualquier reproductor web puede leerlos sin nuestro motor | `docs/CAUCE_PACK_FORMAT.md` |
| Instrumento identificado por `"<paquete>:<instrumento>"` + versión, sin rutas | El mismo proyecto funciona en el móvil, en Windows y en una web con otra organización de archivos | `Session::setSamplerInstrument`, pruebas |
| SHA-256 por archivo | Un servidor o CDN puede servir los archivos y el cliente comprobar que no cambiaron | `cauce-pack.json` |
| Proyecto v4 solo cuando hace falta, v3 intacto | Un lector de v3 ya escrito no se rompe con proyectos que no usan sonidos 0.9 | prueba `sampler_project_integration_v4_only_when_used_and_undo` |
| Paquete para DAW (0.6): stems WAV + MIDI + mezcla | Intercambio que no depende de nada nuestro | `exportDaw` |
| Motor C++17 sin dependencias del sistema | Compilable a WebAssembly (no probado: ver §3) | `docs/ARCHITECTURE.md` |

## 2. Qué conviene preparar después (no hecho)

1. **Manifiesto público del catálogo** (`catalog.json` con la lista de paquetes, versiones, tamaños y hashes) para que
   una web sepa qué sonidos existen sin descargar los ZIP.
2. **Licencia propia definitiva** de los sonidos sintetizados por CAUCESOUND (hoy `LicenseRef-CauceSound-Content`,
   texto provisional). Sin esto no se deben servir públicamente desde la web.
3. **Versión «web» de cada paquete**: OGG/Opus además de WAV reduciría 5-10 veces la descarga. Solo si la web lo pide.
4. **Prueba de lectura cruzada**: cargar los SFZ de los paquetes en otro reproductor (p. ej. sfizz de escritorio) para
   demostrar que no dependen de nuestro motor.
5. **Migración v4 → formato común**, solo si se acuerda un formato compartido, con prueba automática de ida y vuelta.
   No se cambiará el formato v3/v4 sin esa prueba.

## 3. Qué falta validar

- Compilación del motor a WebAssembly: **no probada** (instalar Emscripten quedó pendiente de autorización de Diego).
- Rendimiento del sampler en un navegador: desconocido. En nube x86 son ~10 ms de CPU por segundo de audio con
  32 voces.
- Memoria en navegadores móviles: el presupuesto de 160 MB del motor puede ser demasiado para una pestaña en un
  teléfono; habría que bajarlo o cargar por capas.

## 4. Preguntas para Ronaldo (para cuando Diego decida hablarlo)

1. ¿Qué motor de audio usa el DAW web (Web Audio nativo, Tone.js, otro)? ¿Lee SFZ o tendríamos que darle otro mapa?
2. ¿Dónde viven los sonidos en su arquitectura: en su servidor, en un CDN, en el navegador del usuario?
3. ¿Qué formato de proyecto usa? ¿Le interesaría leer `project.json` v3/v4 o prefiere intercambiar el paquete para DAW?
4. ¿Tiene requisitos de licencia para el contenido que sirve (atribución visible, sin ShareAlike, etc.)?
5. ¿Le serviría un catálogo JSON de paquetes con hashes, o ya tiene su propio sistema de librería?
6. ¿Cuánto puede pesar como máximo una descarga de sonidos en su web?
7. ¿Usa una cuenta de usuario para guardar favoritos y proyectos? (CAUCESOUND hoy es 100 % sin conexión y sin cuentas.)

## 5. Lo que NO se ha hecho a propósito

- No se ha buscado ni abierto el código del DAW web, ni DigitalOcean, ni Cauce Service.
- No se ha cambiado ningún formato para adaptarlo a algo que no conocemos.
- No se ha creado ningún servidor, tienda ni descarga en línea: la importación de paquetes es local (archivo ZIP).
