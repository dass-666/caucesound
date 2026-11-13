#!/usr/bin/env python3
"""Convierte cantante_holografica.svg (arte original de VocalShift) en SingerArt.kt.

Uso: python3 -I tools/svg_to_singer_kt.py <cantante_holografica.svg> app/src/main/java/com/caucesound/app/ui/SingerArt.kt

Solo traduce la geometría y los atributos de pintura a datos Kotlin; los degradados, la máscara, el recorte y el
desenfoque los resuelve SingerIllustration.kt con los mismos valores del SVG. El fondo del SVG (rect #bg) se omite:
el fondo del panel se dibuja aparte, en coordenadas del panel, como pide la especificación.
"""
import sys
import xml.etree.ElementTree as ET

NS = '{http://www.w3.org/2000/svg}'
src, dst = sys.argv[1], sys.argv[2]
root = ET.parse(src).getroot()

paths = []          # cadenas d únicas
def pid(d):
    d = ' '.join(d.split())
    if d not in paths: paths.append(d)
    return paths.index(d)

def paint(v):
    if v is None or v == 'none': return 'null'
    if v.startswith('url(#'): return 'SingerGrad.' + v[5:-1].upper()
    v = v.lstrip('#')
    if len(v) == 3: v = ''.join(c * 2 for c in v)
    return f'SingerSolid(0xFF{v.upper()})'

ops = []
def emit(e, extra=''):
    tag = e.tag.replace(NS, '')
    op = float(e.get('opacity', '1'))
    fill = paint(e.get('fill', '#000' if tag != 'path' or e.get('stroke') is None else 'none'))
    if tag == 'path' and e.get('fill') is None and e.get('stroke') is not None: fill = 'null'
    stroke = paint(e.get('stroke'))
    fa = op * float(e.get('fill-opacity', '1'))
    sa = op * float(e.get('stroke-opacity', '1'))
    w = float(e.get('stroke-width', '1'))
    cap = 'true' if e.get('stroke-linecap') == 'round' else 'false'
    blur = 'true' if e.get('filter') == 'url(#glow)' else 'false'
    mask = 'true' if e.get('mask') == 'url(#mf)' else 'false'
    if tag == 'path': geo = f'path = {pid(e.get("d"))}'
    elif tag == 'ellipse': geo = f'oval = floatArrayOf({e.get("cx")}f, {e.get("cy")}f, {e.get("rx")}f, {e.get("ry")}f)'
    elif tag == 'circle': geo = f'oval = floatArrayOf({e.get("cx")}f, {e.get("cy")}f, {e.get("r")}f, {e.get("r")}f)'
    elif tag == 'rect': geo = 'rect = true'
    else: raise SystemExit('elemento no soportado: ' + tag)
    ops.append(f'        SingerOp({geo}, fill = {fill}, fillAlpha = {fa:.3f}f, stroke = {stroke}, strokeAlpha = {sa:.3f}f, width = {w}f, round = {cap}, blur = {blur}, fade = {mask}{extra}),')

for e in root:
    tag = e.tag.replace(NS, '')
    if tag in ('defs', 'metadata'): continue
    if tag == 'rect' and e.get('fill') == 'url(#bg)': continue  # fondo: lo pinta el panel
    if tag == 'g':
        for c in e: emit(c, ', clipSilhouette = true')
        continue
    emit(e)

clip = root.find(f'{NS}defs/{NS}clipPath/{NS}path').get('d')
sil = pid(clip)
contour = [pid(e.get('d')) for e in root if e.get('filter') == 'url(#glow)'][0]
lips = [pid(e.get('d')) for e in root if e.get('fill') == 'url(#lips)']
out = ['// GENERADO por tools/svg_to_singer_kt.py a partir de cantante_holografica.svg. No editar a mano.',
       '// Arte original de VocalShift AI Studio (vectorial). Coordenadas en unidades de diseño (caja de 1000).',
       'package com.caucesound.app.ui', '',
       '@Suppress("LargeClass", "MaxLineLength")',
       'internal object SingerArt {',
       f'    const val SILHOUETTE = {sil}', f'    const val CONTOUR = {contour}', f'    val LIPS = intArrayOf({", ".join(map(str, lips))})', '    val D: Array<String> = arrayOf(']
out += [f'        "{d}",' for d in paths]
out += ['    )', '    val OPS: List<SingerOp> = listOf(']
out += ops
out += ['    )', '}', '']
open(dst, 'w').write('\n'.join(out))
print(len(paths), 'trazados,', len(ops), 'operaciones')
