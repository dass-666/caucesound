// Calidad sonora medible de los presets de Cauce Synth y de los kits de Cauce Drums. No sustituye la escucha:
// detecta defectos objetivos (desafinación, aliasing, saltos de volumen entre presets, DC, colas que no terminan,
// clics al soltar, desequilibrio dentro de un kit) e imprime una tabla para comparar versiones.
#include "Test.h"

#include "cs/Instruments.h"
#include "cs/Midi.h"
#include "cs/Processor.h"
#include "../src/Dsp.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <string>
#include <vector>

using namespace cs;

namespace
{
constexpr double kSr = 48000;

std::shared_ptr<Instrument> makeInstrument (const std::string& type, const std::string& state = {})
{
    auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor (type));
    if (inst && ! state.empty()) inst->loadState (state);
    if (inst) inst->prepare (kSr, 512);
    return inst;
}

void setParam (AudioProcessor& p, const char* id, float v) { p.setParameter (parameterIndex (p, id), v); }

// Toca una nota (o golpe) y devuelve el canal izquierdo: noteSec de nota y tailSec de cola tras soltarla.
std::vector<float> play (Instrument& inst, int key, int vel, double noteSec, double tailSec, uint8_t channelStatus = 0x90)
{
    const size_t total = (size_t) ((noteSec + tailSec) * kSr), off = (size_t) (noteSec * kSr);
    std::vector<float> outL (total), l (256), r (256);
    for (size_t done = 0; done < total; done += 256)
    {
        const int n = (int) std::min<size_t> (256, total - done);
        MidiEvent ev[2];
        int count = 0;
        if (done == 0) { ev[count].frameOffset = 0; ev[count].status = channelStatus; ev[count].data1 = (uint8_t) key; ev[count].data2 = (uint8_t) vel; ++count; }
        if (off >= done && off < done + (size_t) n)
        { ev[count].frameOffset = (int) (off - done); ev[count].status = (uint8_t) ((channelStatus & 0x0F) | 0x80); ev[count].data1 = (uint8_t) key; ++count; }
        if (count) inst.handleMidi (ev, count);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        float* io[2] = { l.data(), r.data() };
        inst.process (io, 2, n);
        std::copy (l.begin(), l.begin() + n, outL.begin() + (long) done);
    }
    return outL;
}

double rms (const std::vector<float>& v, size_t a, size_t b)
{
    b = std::min (b, v.size());
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
double db (double x) { return 20 * std::log10 (std::max (x, 1e-12)); }

// Frecuencia fundamental por autocorrelación normalizada con interpolación parabólica.
double pitchHz (const std::vector<float>& v, size_t a, size_t len, double fmin, double fmax)
{
    const int lagMin = (int) (kSr / fmax), lagMax = (int) (kSr / fmin);
    std::vector<double> c ((size_t) lagMax + 2, 0.0);
    for (int lag = lagMin - 1; lag <= lagMax + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i < a + len; ++i) { s += (double) v[i] * v[i + (size_t) lag]; e1 += (double) v[i] * v[i]; e2 += (double) v[i + (size_t) lag] * v[i + (size_t) lag]; }
        c[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-20);
    }
    // Primer máximo que supera el 90 % del máximo global (evita confundir con múltiplos del periodo).
    double best = 0;
    for (int lag = lagMin; lag <= lagMax; ++lag) best = std::max (best, c[(size_t) lag]);
    int lag = lagMin;
    for (int k = lagMin; k <= lagMax; ++k)
        if (c[(size_t) k] >= 0.9 * best && c[(size_t) k] >= c[(size_t) k - 1] && c[(size_t) k] >= c[(size_t) k + 1]) { lag = k; break; }
    const double y0 = c[(size_t) lag - 1], y1 = c[(size_t) lag], y2 = c[(size_t) lag + 1];
    const double d = (y0 - 2 * y1 + y2) != 0 ? 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) : 0;
    return kSr / ((double) lag + d);
}

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w = std::polar (1.0, -2 * M_PI / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn (1);
            for (size_t k = 0; k < len / 2; ++k, wn *= w)
            {
                const auto u = a[i + k], t = wn * a[i + k + len / 2];
                a[i + k] = u + t; a[i + k + len / 2] = u - t;
            }
        }
    }
}

// Energía fuera de los armónicos de f0 respecto de la total (aliasing + ruido), en dB.
double inharmonicDb (const std::vector<float>& v, size_t a, double f0)
{
    const size_t n = 16384;
    std::vector<std::complex<double>> x (n);
    // Ventana Blackman-Harris de 4 términos (lóbulos laterales a -92 dB): la fuga de la ventana no se confunde con aliasing.
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2 * M_PI * (double) i / (double) (n - 1);
        x[i] = v[a + i] * (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
    }
    fft (x);
    double total = 0, harmonic = 0;
    const double binHz = kSr / (double) n;
    for (size_t k = 3; k < n / 2; ++k)
    {
        const double e = std::norm (x[k]);
        total += e;
        const double h = (double) k * binHz / f0, dist = std::fabs (h - std::round (h)) * f0 / binHz;
        if (std::round (h) >= 1 && dist <= 6) harmonic += e;
    }
    return 10 * std::log10 (std::max (total - harmonic, 1e-30) / total);
}
} // namespace

TEST (synth_presets_are_in_tune_balanced_and_end_cleanly)
{
    const auto& presets = synthPresets();
    CHECK (presets.size() == 17);
    // Grupos: los bajos se miden aparte (a igual RMS, los graves suenan más suaves; ir más fuertes es intencionado).
    double louder[2] = { -1e9, -1e9 }, quieter[2] = { 1e9, 1e9 };
    std::printf ("    measure: preset        | afinación (cents) | RMS máx 50 ms | pico   | DC       | cola final | salto al soltar\n");
    for (const auto& p : presets)
    {
        auto inst = makeInstrument ("cs.synth", synthPresetState (p.id));
        CHECK (inst != nullptr);
        if (! inst) continue;
        const int octave = (int) std::lround (inst->getParameter (parameterIndex (*inst, "octave")));
        const int key = 57; // La 3 (220 Hz) antes de la octava del preset
        const double expect = 440.0 * std::pow (2.0, (key - 69 + 12 * octave) / 12.0);
        const auto v = play (*inst, key, 100, 1.0, 4.0);
        const double f = pitchHz (v, (size_t) (0.25 * kSr), 4096, expect / 1.5, expect * 1.5);
        const double cents = 1200 * std::log2 (f / expect);
        // Sonoridad momentánea: el máximo de RMS en ventanas de 50 ms (vale para notas sostenidas y percusivas).
        double level = 0;
        for (size_t w = 0; w + 2400 <= (size_t) kSr; w += 1200) level = std::max (level, rms (v, w, w + 2400));
        float peak = 0;
        double sum = 0;
        for (float x : v) { peak = std::max (peak, std::fabs (x)); sum += x; }
        const double dc = sum / (double) v.size();
        const double tailEnd = rms (v, v.size() - (size_t) (0.05 * kSr), v.size());
        // Salto en la muestra del Note Off (clic): diferencia entre muestras respecto del pico.
        const size_t off = (size_t) kSr;
        float jump = 0;
        for (size_t i = off - 4; i < off + 64; ++i) jump = std::max (jump, std::fabs (v[i + 1] - v[i]));
        float typical = 0;
        for (size_t i = off - 2000; i < off - 100; ++i) typical = std::max (typical, std::fabs (v[i + 1] - v[i]));
        std::printf ("    measure: %-13s | %+8.2f          | %6.1f dB     | %.3f  | %+.1e | %6.1f dB  | %.2fx del normal\n",
                     p.id.c_str(), cents, db (level), peak, dc, db (tailEnd), typical > 0 ? jump / typical : 0.0);
        CHECK_MSG (std::fabs (cents) < 5.0, p.id + " desafinado");
        CHECK_MSG (peak < 1.0f && level > 0.01, p.id + " nivel");
        CHECK_MSG (std::fabs (dc) < 0.01 * std::max (level, 0.01), p.id + " DC");
        CHECK_MSG (db (tailEnd) < -90.0, p.id + " la cola no termina");
        CHECK_MSG (typical == 0 || jump <= 1.5f * typical, p.id + " clic al soltar");
        const int g = p.category == "bass" ? 1 : 0; // por categoría (cauce_808 y cauce_sub son bajos sin «bass» en el id)
        louder[g] = std::max (louder[g], db (level));
        quieter[g] = std::min (quieter[g], db (level));
    }
    MEASURE ("diferencia de volumen entre presets: melódicos %.1f dB, bajos %.1f dB; bajos sobre melódicos %.1f dB",
             louder[0] - quieter[0], louder[1] - quieter[1], quieter[1] - louder[0]);
    // Cambiar de preset dentro del mismo papel no obliga a reajustar el mezclador.
    CHECK (louder[0] - quieter[0] < 6.0);
    CHECK (louder[1] - quieter[1] < 6.0);
}

TEST (synth_oscillators_have_low_aliasing_at_high_notes)
{
    const char* names[] = { "seno", "triángulo", "sierra", "cuadrada" };
    for (int wave = 0; wave < 4; ++wave)
    {
        auto inst = makeInstrument ("cs.synth");
        setParam (*inst, "osc1Wave", (float) wave);
        setParam (*inst, "osc2Mix", 0.0f);
        setParam (*inst, "cutoffHz", 18000.0f);
        setParam (*inst, "resonance", 0.0f);
        setParam (*inst, "sustain", 1.0f);
        for (int key : { 84, 96 }) // Do 6 (1047 Hz) y Do 7 (2093 Hz)
        {
            const double f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
            const auto v = play (*inst, key, 100, 1.0, 0.6);
            const double ih = inharmonicDb (v, (size_t) (0.3 * kSr), f0);
            MEASURE ("oscilador %-10s nota %d (%.0f Hz): energía fuera de los armónicos %.1f dB", names[wave], key, f0, ih);
            // PolyBLEP de 2 muestras: el residuo medido ronda -38/-41 dB en Do 7 con sierra y cuadrada sin filtrar.
            // El umbral fija ese nivel para detectar regresiones; mejorarlo (sobremuestreo x2) queda pendiente.
            CHECK_MSG (ih < (wave == 0 ? -80.0 : -36.0), std::string (names[wave]) + " con más aliasing que el medido");
        }
    }
    // Uso real: el preset Lead (dos sierras con su filtro) en notas agudas.
    auto lead = makeInstrument ("cs.synth", synthPresetState ("lead"));
    setParam (*lead, "osc2Mix", 0.0f); // un solo oscilador para poder separar armónicos de aliasing
    for (int key : { 84, 96 })
    {
        const double f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        const auto v = play (*lead, key, 100, 1.0, 0.6);
        const double ih = inharmonicDb (v, (size_t) (0.4 * kSr), f0);
        MEASURE ("preset lead (1 oscilador, con su filtro) nota %d: energía fuera de los armónicos %.1f dB", key, ih);
        CHECK (ih < -50.0);
    }
}

TEST (drum_kits_are_balanced_short_and_clean)
{
    const auto& kits = drumKits();
    CHECK (kits.size() == 8);
    for (int k = 0; k < (int) kits.size(); ++k)
    {
        auto inst = makeInstrument ("cs.drums", "{\"kit\":" + std::to_string (k) + "}");
        CHECK (inst != nullptr);
        if (! inst) continue;
        std::string row;
        double kickPeak = 0, snarePeak = 0, minPeak = 1e9, maxPeak = -1e9;
        for (int lane = 0; lane < kDrumLanes; ++lane)
        {
            const auto v = play (*inst, kDrumBaseNote + lane, 127, 0.05, 3.0, 0x99);
            float peak = 0;
            double sum = 0;
            for (float x : v) { peak = std::max (peak, std::fabs (x)); sum += x; }
            size_t last = 0;
            for (size_t i = 0; i < v.size(); ++i) if (std::fabs (v[i]) > peak * 0.001f) last = i; // -60 dB
            const double durMs = 1000.0 * (double) last / kSr, dc = sum / (double) v.size();
            char cell[64];
            std::snprintf (cell, sizeof cell, " %s %.1f dB/%.0f ms", kits[(size_t) k].laneNames[(size_t) lane].c_str(), db (peak), durMs);
            row += cell;
            if (lane == 0) kickPeak = db (peak);
            if (lane == 1) snarePeak = db (peak);
            minPeak = std::min (minPeak, db (peak));
            maxPeak = std::max (maxPeak, db (peak));
            CHECK_MSG (peak < 1.0f && db (peak) > -24.0, kits[(size_t) k].id + " carril " + std::to_string (lane) + " nivel");
            CHECK_MSG (durMs < 2500.0, kits[(size_t) k].id + " carril " + std::to_string (lane) + " cola demasiado larga");
            CHECK_MSG (std::fabs (dc) < 0.002, kits[(size_t) k].id + " carril " + std::to_string (lane) + " DC");
            inst->allNotesOff (true);
        }
        MEASURE ("kit %-11s:%s", kits[(size_t) k].id.c_str(), row.c_str());
        MEASURE ("kit %-11s: bombo vs caja %.1f dB; rango de picos %.1f dB", kits[(size_t) k].id.c_str(), kickPeak - snarePeak, maxPeak - minPeak);
        CHECK (std::fabs (kickPeak - snarePeak) < 6.0); // bombo y caja, la base del beat, a niveles parecidos
    }
}

TEST (fast_sine_matches_std_sin)
{
    double err = 0;
    for (int i = 0; i < 1000000; ++i)
    {
        const double ph = (double) i / 1000000.0;
        err = std::max (err, std::fabs (cs::dsp::sin2pi (ph) - std::sin (2 * M_PI * ph)));
    }
    MEASURE ("seno rápido del oscilador: error máximo %.2g (%.0f dB)", err, 20 * std::log10 (err));
    CHECK (err < 1e-6);
}

// Parámetros nuevos del synth (caída de tono y fuerza → filtro): hacen lo que dicen, valen 0 por defecto y un
// estado guardado antes de que existieran suena exactamente igual que antes.
TEST (synth_pitch_envelope_and_velocity_filter_work_and_old_states_are_unchanged)
{
    // 1. Compatibilidad: un estado sin los parámetros nuevos = los mismos parámetros con los nuevos a 0.
    const std::string old = R"({"osc1Wave":2,"osc2Wave":2,"osc2Mix":0.5,"osc2Detune":12,"attackMs":6,"decayMs":400,"sustain":0.75,
        "releaseMs":220,"cutoffHz":2800,"resonance":0.3,"filterEnv":0.35,"filterDecayMs":350,"volumeDb":-9,"polyphony":6})";
    auto a = makeInstrument ("cs.synth", old);
    auto b = makeInstrument ("cs.synth", old);
    CHECK (a && b);
    setParam (*b, "pitchEnvSt", 0); setParam (*b, "velToFilter", 0); setParam (*b, "pitchDecayMs", 200);
    CHECK (a->getParameter (parameterIndex (*a, "pitchEnvSt")) == 0.0f && a->getParameter (parameterIndex (*a, "velToFilter")) == 0.0f);
    const auto va = play (*a, 60, 100, 0.5, 0.5), vb = play (*b, 60, 100, 0.5, 0.5);
    float diff = 0, peak = 0;
    for (size_t i = 0; i < va.size(); ++i) { diff = std::max (diff, std::fabs (va[i] - vb[i])); peak = std::max (peak, std::fabs (va[i])); }
    CHECK (peak > 0.01f && diff == 0.0f);

    // 2. Caída de tono: seno con 12 semitonos y 50 ms. Empieza cerca de una octava arriba y termina afinado.
    auto s = makeInstrument ("cs.synth");
    setParam (*s, "osc1Wave", 0); setParam (*s, "cutoffHz", 18000); setParam (*s, "sustain", 1); setParam (*s, "attackMs", 0.5f);
    setParam (*s, "pitchEnvSt", 12); setParam (*s, "pitchDecayMs", 50);
    const auto v = play (*s, 57, 100, 1.0, 0.2);
    auto periodHz = [&] (size_t from) // frecuencia de un solo ciclo (entre dos cruces por cero ascendentes) desde 'from'
    {
        size_t first = 0, last = 0; int n = 0;
        for (size_t i = from + 1; i < v.size() && n < 2; ++i)
            if (v[i - 1] < 0 && v[i] >= 0) { if (n == 0) first = i; last = i; ++n; }
        return n >= 2 ? kSr * (n - 1) / (double) (last - first) : 0.0;
    };
    const double early = periodHz (0), late = pitchHz (v, (size_t) (0.5 * kSr), 4096, 150, 330);
    MEASURE ("caída de tono 12 st / 50 ms sobre La 220 Hz: el segundo ciclo va a %.0f Hz, a 0,5 s está en %.2f Hz (%.2f cents)", early, late, 1200 * std::log2 (late / 220.0));
    CHECK (early > 330.0 && early < 445.0);
    CHECK (std::fabs (1200 * std::log2 (late / 220.0)) < 1.0);

    // 3. Fuerza → filtro: con velToFilter, una nota fuerte tiene más energía aguda (respecto a su nivel) que una suave.
    auto bright = [&] (float velToFilter, int vel)
    {
        auto k = makeInstrument ("cs.synth");
        setParam (*k, "osc1Wave", 2); setParam (*k, "cutoffHz", 800); setParam (*k, "sustain", 1); setParam (*k, "velToFilter", velToFilter);
        const auto x = play (*k, 48, vel, 0.5, 0.1);
        double hi = 0, all = 0; // energía de la derivada (realza agudos) frente a la total
        for (size_t i = (size_t) (0.1 * kSr) + 1; i < (size_t) (0.4 * kSr); ++i) { hi += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]); all += (double) x[i] * x[i]; }
        return 10.0 * std::log10 (hi / all);
    };
    const double loudOn = bright (1.0f, 127), softOn = bright (1.0f, 30), loudOff = bright (0.0f, 127), softOff = bright (0.0f, 30);
    MEASURE ("brillo relativo (dB): con fuerza→filtro, fuerte %.1f / suave %.1f; sin él, fuerte %.1f / suave %.1f", loudOn, softOn, loudOff, softOff);
    CHECK (loudOn - softOn > 3.0);
    CHECK (std::fabs (loudOff - softOff) < 0.5);
}

// Misión 0.5 (Sonido 2.0). Se probó añadir un control «harmonics» al sintetizador (primero un segundo armónico, luego
// rectificación antes de la saturación) para que el 808 se oiga mejor en altavoces pequeños. La medición lo descartó:
// el 808 ya tiene su tercer armónico (165 Hz para un La de 55 Hz) a unos -10 dB gracias a «drive», y el añadido no
// subía la energía por encima de 150 Hz (-12,5 dB sin él, -13,6 dB con él). No se incorporó. Esta prueba fija la
// propiedad que sí existe, para que un cambio futuro del preset no la pierda sin darse cuenta.
TEST (cauce_808_keeps_harmonics_above_its_fundamental_for_small_speakers)
{
    auto tone = [] (const std::vector<float>& x, size_t from, size_t n, double hz)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * 3.14159265358979 * (double) i / (double) n);
            const double ph = 2 * 3.14159265358979 * hz * (double) i / kSr;
            re += x[from + i] * w * std::cos (ph); im += x[from + i] * w * std::sin (ph);
        }
        return 10.0 * std::log10 ((re * re + im * im) / ((double) n * n) + 1e-20);
    };
    auto s = makeInstrument ("cs.synth", synthPresetState ("cauce_808"));
    CHECK (s != nullptr);
    const auto x = play (*s, 45, 110, 1.2, 0.3); // La1: con la octava -1 del preset suena a 55 Hz
    const size_t from = (size_t) (0.3 * kSr), n = 32768;
    const double f0 = 55.0, h3 = tone (x, from, n, 3 * f0) - tone (x, from, n, f0);
    double hi = 0, all = 0, z1 = 0, z2 = 0, w1 = 0, w2 = 0;
    const double k = std::tan (3.14159265358979 * 150.0 / kSr), q = 0.7071, norm = 1 / (1 + k / q + k * k);
    const double b0 = norm, b1 = -2 * norm, b2 = norm, a1 = 2 * (k * k - 1) * norm, a2 = (1 - k / q + k * k) * norm;
    for (size_t i = 0; i < from + n; ++i)
    {
        double y = b0 * x[i] + z1; z1 = b1 * x[i] - a1 * y + z2; z2 = b2 * x[i] - a2 * y;
        double y2 = b0 * y + w1; w1 = b1 * y - a1 * y2 + w2; w2 = b2 * y - a2 * y2;
        if (i >= from) { hi += y2 * y2; all += (double) x[i] * x[i]; }
    }
    const double above = 10.0 * std::log10 (hi / all + 1e-20);
    MEASURE ("808 a 55 Hz: tercer armónico (165 Hz) a %.1f dB del fundamental; energía por encima de 150 Hz: %.1f dB del total", h3, above);
    CHECK (h3 > -14.0);
    CHECK (above > -15.0);
    CHECK (std::fabs (1200 * std::log2 (pitchHz (x, from, 16384, 40, 80) / f0)) < 3.0); // afinado
}
