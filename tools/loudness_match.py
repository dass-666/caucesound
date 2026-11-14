#!/usr/bin/env python3
"""Iguala la sonoridad de varios WAV para compararlos de oído sin que gane «el que suena más fuerte».

Uso: loudness_match.py <carpeta de salida> <grupo>=<a.wav>,<b.wav>,... [<grupo>=...]

Para cada grupo mide la sonoridad integrada de cada archivo (ITU-R BS.1770-4: ponderación K y doble puerta, el
mismo método que usan las plataformas de música), elige un nivel común que ninguno supere sin recortar (pico de
muestra <= -1,5 dBFS, como mucho -16 LUFS) y escribe copias de 16 bits con esa ganancia. No comprime ni limita: solo
multiplica por una constante, así que la comparación conserva la dinámica de cada versión.

Limitaciones: mide el pico de muestra, no el pico real entre muestras; solo 48 kHz; necesita numpy.
"""
import json
import os
import sys
import wave

import numpy as np


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, width, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / 8388608.0
    else:
        raise SystemExit(f"{path}: solo WAV de 16 o 24 bits")
    return x.reshape(-1, ch), sr


def k_weight(x, sr):
    """Ponderación K aplicada en frecuencia (respuesta exacta de los dos filtros de la norma)."""
    if sr == 48000:
        s1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
        s2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])
    else:
        raise SystemExit("solo 48 kHz (la frecuencia de los proyectos de CAUCESOUND)")
    n = len(x)
    z = np.exp(-1j * 2 * np.pi * np.fft.rfftfreq(n))
    h = np.ones_like(z)
    for b, a in (s1, s2):
        h *= (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * h[:, None], n=n, axis=0)


def lufs(x, sr):
    y = k_weight(x, sr)
    block, hop = int(0.4 * sr), int(0.1 * sr)
    if len(y) < block:
        return -70.0
    c = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y * y, axis=0)])
    starts = np.arange(0, len(y) - block + 1, hop)
    z = ((c[starts + block] - c[starts]) / block).sum(axis=1)  # canales L y R con peso 1
    loud = -0.691 + 10 * np.log10(np.maximum(z, 1e-12))
    keep = loud > -70.0
    if not keep.any():
        return -70.0
    rel = -0.691 + 10 * np.log10(z[keep].mean()) - 10.0
    keep &= loud > rel
    return float(-0.691 + 10 * np.log10(z[keep].mean()))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    report = {}
    for spec in sys.argv[2:]:
        group, files = spec.split("=", 1)
        items = []
        for path in files.split(","):
            x, sr = read_wav(path)
            items.append((path, x, sr, lufs(x, sr), 20 * np.log10(max(np.abs(x).max(), 1e-9))))
        target = min([-16.0] + [l - p - 1.5 for _, _, _, l, p in items])
        rows = []
        for path, x, sr, l, p in items:
            gain = target - l
            y = x * 10 ** (gain / 20)
            q = np.round(y * 32767.0)
            clipped = int((np.abs(q) > 32767).sum())
            q = np.clip(q, -32768, 32767).astype("<i2")
            name = os.path.basename(path)
            with wave.open(os.path.join(out, name), "wb") as w:
                w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr)
                w.writeframes(q.tobytes())
            check, _ = read_wav(os.path.join(out, name))
            rows.append({"archivo": name, "lufsOriginal": round(l, 2), "picoOriginalDb": round(p, 2), "gananciaDb": round(gain, 2),
                         "lufsFinal": round(lufs(check, sr), 2), "picoFinalDb": round(20 * np.log10(max(np.abs(check).max(), 1e-9)), 2),
                         "muestrasRecortadas": clipped, "segundos": round(len(x) / sr, 1)})
            print(f"{group:11s} {name:48s} {l:6.1f} LUFS, pico {p:5.1f} dB -> {gain:+5.1f} dB = {rows[-1]['lufsFinal']:6.1f} LUFS, pico {rows[-1]['picoFinalDb']:5.1f} dB, recortes {clipped}")
        report[group] = {"objetivoLufs": round(target, 2), "archivos": rows}
    with open(os.path.join(out, "sonoridad.json"), "w") as f:
        json.dump(report, f, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()
