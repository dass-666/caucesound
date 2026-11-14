#!/usr/bin/env bash
# Descarga de VCSL (CC0, Versilian Studios) SOLO los archivos que usa el generador de paquetes, en el commit fijado.
# Uso: tools/fetch_vcsl.sh <carpeta destino>     (luego: cauce_packs <carpeta destino> <salida> ...)
set -euo pipefail
COMMIT=c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e
DEST="${1:?carpeta destino}"
LIST="$(dirname "$0")/../desktop/tools/packbuilder/vcsl-drum-files.txt"
n=0
while IFS= read -r rel; do
  [ -z "$rel" ] && continue
  out="$DEST/$rel"
  if [ ! -s "$out" ]; then
    mkdir -p "$(dirname "$out")"
    url="https://raw.githubusercontent.com/sgossner/VCSL/$COMMIT/$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' "$rel")"
    curl -fsS -L --retry 3 --max-time 120 -o "$out.part" "$url" && mv "$out.part" "$out"
  fi
  head -c 4 "$out" | grep -q RIFF || { echo "no es WAV: $rel" >&2; exit 1; }
  n=$((n+1))
done < "$LIST"
echo "VCSL: $n archivos en $DEST"
