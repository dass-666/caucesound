#!/usr/bin/env python3
"""Mide muestras de un paquete Cauce (no sustituye al oído): pico, RMS, tiempo de ataque, duración útil, centroide
espectral y reparto de energía por bandas. Uso: tools/analyze_samples.py <archivo.wav>..."""
import sys, wave
import numpy as np


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return (x.reshape(-1, ch).mean(axis=1) if ch > 1 else x), sr


def measure(path):
    x, sr = load(path)
    peak = np.max(np.abs(x)) + 1e-12
    env = np.abs(x)
    att = np.argmax(env >= 0.9 * peak) / sr * 1000.0
    above = np.nonzero(env > peak * 10 ** (-40 / 20))[0]
    dur = (above[-1] + 1) / sr if len(above) else 0.0
    spec = np.abs(np.fft.rfft(x)) ** 2  # sin ventana: son golpes que empiezan y acaban en cero, y una ventana borraría el ataque
    f = np.fft.rfftfreq(len(x), 1 / sr)
    tot = spec.sum() + 1e-20
    band = lambda lo, hi: 100.0 * spec[(f >= lo) & (f < hi)].sum() / tot
    return {"peak": peak, "rms_db": 10 * np.log10(np.mean(x ** 2) + 1e-20), "attack_ms": att, "dur_s": dur,
            "centroid": (f * spec).sum() / tot, "sub": band(0, 80), "low": band(80, 150), "lomid": band(150, 400),
            "mid": band(400, 2500), "high": band(2500, 24000), "dc": abs(np.mean(x))}


if __name__ == "__main__":
    print(f'{"archivo":58s} pico  rms    atq_ms dur_s centro  <80  80-150 150-400 0.4-2.5k >2.5k')
    for p in sys.argv[1:]:
        m = measure(p)
        print(f'{p[-58:]:58s} {m["peak"]:.2f} {m["rms_db"]:6.1f} {m["attack_ms"]:6.1f} {m["dur_s"]:5.2f} {m["centroid"]:6.0f} {m["sub"]:5.1f} {m["low"]:6.1f} {m["lomid"]:7.1f} {m["mid"]:8.1f} {m["high"]:5.1f}' + ("  DC!" if m["dc"] > 0.01 else ""))
