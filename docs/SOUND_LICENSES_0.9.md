# Sonidos: licencias y procedencia (misión 0.9, fase D)

Consultado el **2026-11-13** desde la nube (sin cuentas, sin pagos, sin aceptar contratos). Distinción importante:

- **Usar en canciones**: el usuario publica música hecha con el sonido.
- **Redistribuir en la app**: CAUCESOUND entrega los archivos de muestra dentro del APK o en un paquete. Es la condición
  más exigente y la que se ha verificado.

> No soy abogado. CC0 es la licencia más permisiva que existe (renuncia a derechos), pero antes de una versión
> comercial conviene una revisión jurídica de los textos de `LICENSE.txt` de los paquetes propios (marcados provisionales).

## Bibliotecas usadas en la 0.9

| | VCSL (Versilian Community Sample Library) | Karoryfer «Black And Blue Basses» |
|---|---|---|
| Autor | Versilian Studios LLC | Karoryfer Samples |
| URL | https://github.com/sgossner/VCSL | https://github.com/sfzinstruments/karoryfer.black-and-blue-basses |
| Versión usada | commit `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e` | commit `6e7d674cdb41be7a54dbccb15472401ad01099b9` (v1.002) |
| Licencia | CC0 1.0 Universal | CC0 1.0 Universal (archivo `license` del repositorio) |
| Evidencia de redistribución | Texto CC0 + README del repositorio (permite uso en software comercial) | Texto CC0 completo en el repositorio |
| Uso comercial | Sí | Sí |
| Atribución | No obligatoria (se da igualmente en PROVENANCE.md y en el explorador) | No obligatoria (se da igualmente) |
| Restricción a derivados | Ninguna | Ninguna |
| Formato original | WAV 24 bits / 48 kHz, SFZ | WAV 24 bits / 44,1 kHz mono, SFZ |
| Qué se usó | Cajas, palmas, bongó, conga, güiro, platillos y piano Kawai (capas v1, v3, v4) | Bajo «darkblack», articulación normal, toma rr1, dinámicas p/mf/f |
| Calidad (juicio técnico, no de oído) | Grabación limpia y seca; el piano tiene 5 capas y round robin | Bajo real con 4 dinámicas y 4 tomas; afinación medida dentro de ±0,2 semitonos |
| Apto para Android | Sí, tras recortar y remuestrear | Sí, tras recortar a 1,8-2,6 s |

Cada archivo de salida está anotado en el `PROVENANCE.md` de su paquete con el archivo de origen exacto y el procesado.
Los hashes SHA-256 de cada archivo están en `cauce-pack.json`; los de los ZIP, abajo.

## Paquetes entregados

| Paquete | Licencia | Origen | ZIP (SHA-256) |
|---|---|---|---|
| cauce.signature.drums 1.0.0 | LicenseRef-CauceSound-Content AND CC0-1.0 | Bombos, charles y percusión sintetizados por CAUCESOUND; cajas, palmas y percusión latina de VCSL | `93b54e64…a5888466cd1` (6,1 MB) |
| cauce.bass.lab 1.0.0 | LicenseRef-CauceSound-Content | 100 % síntesis propia | `fe443508…5165d044e81a` (1,7 MB) |
| cauce.bass.electric 1.0.0 | CC0-1.0 | Karoryfer | `9b255a61…3d4d54b358a6` (8,7 MB) |
| cauce.keys.grand 1.0.0 | CC0-1.0 | VCSL | `b9ac7f45…ae39b91` (42,6 MB, opcional) |

Hashes completos: `/mnt/project-files/caucesound/paquetes-0.9/SHA256SUMS.txt`.

## Investigadas y no usadas (todavía)

| Biblioteca | Licencia comprobada | ¿Redistribuible en la app? | Decisión |
|---|---|---|---|
| VSCO 2 Community Edition | CC0 (GitHub) | Sí | Candidata para cuerdas y vientos en la 1.0 |
| Karoryfer: Big Rusty Drums, Frankensnare, Swirly/Unruly Drums, Growlybass, guitarras eléctricas | CC0 (según sfzinstruments.github.io; verificar cada repositorio antes de usar) | Sí | Candidatas; no se descargaron por tamaño |
| DrumGizmo kits | CC-BY-4.0 | Sí, con atribución visible | Posible; exige pantalla de créditos |
| Salamander Drumkit | CC-BY-SA-3.0 | Con condiciones (ShareAlike sobre el derivado) | **Descartada**: el «compartir igual» complica un producto comercial |
| «Meta Open Sound» | No se encontró como biblioteca de muestras. Solo existen kits de sonidos para prototipos con términos propios | No verificado | **Descartada** hasta tener un texto de licencia claro |
| Guitarras acústicas (nylon/acero) | No se encontró ninguna con licencia clara y calidad suficiente | — | **Pendiente**: no se rellenó la categoría con algo dudoso |

## Lo que nunca se usó

Grabaciones comerciales, fragmentos de canciones, muestras anónimas de Discord, librerías propietarias, contenido que
exija cuenta, pago o aceptar contratos.
