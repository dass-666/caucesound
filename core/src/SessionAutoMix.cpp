// AutoMix (misión 0.9, prototipo): sugerencias de mezcla basadas en REGLAS medibles, no en IA. Ver docs/AUTOMIX.md.
//
// 1. Mide cada pista sola con un render offline (el proyecto no cambia y la reproducción sigue).
// 2. Toma la batería como referencia (o la pista más fuerte si no hay batería) y fija un objetivo por papel y estilo,
//    calibrado con las plantillas aprobadas (tabla en roleOffsetDb).
// 3. Propone mover el fader hacia el objetivo (máximo ±6 dB; nada si la diferencia es menor de 1,5 dB). El panorama
//    se conserva (en este prototipo solo se proponen faders).
// La mezcla no entra en Deshacer (así funciona toda la app); Aplicar guarda los faders anteriores y «Revertir AutoMix»
// los devuelve de una vez.
#include "cs/Session.h"

#include "cs/Composer.h"
#include "cs/Instruments.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace cs
{
using nlohmann::json;
namespace
{
// Objetivos por estilo calibrados con las plantillas TAL CUAL (balance 0.6 aprobado por Diego): `cs_render --automix`
// mide cada papel respecto a la batería en los primeros 16 s. Así una plantilla sin tocar no recibe sugerencias y AutoMix
// solo corrige lo que el usuario desequilibró. Sin estilo: la mediana de las ocho plantillas. Voz: +1 dB sobre la batería
// (convención habitual, NO calibrada con grabaciones reales: pendiente de validar con voces de verdad).
float roleOffsetDb (const std::string& genre, const std::string& role, int style = 1)
{
    struct G { const char* id; float bass, chords, melody, pad; };
    static const G table[] = {
        { "trap", -6.1f, -16.6f, -11.6f, -17.3f },      { "reggaeton", -7.3f, -17.3f, -16.3f, -20.1f },
        { "hiphop", -7.0f, -12.0f, -18.8f, -20.1f },    { "boombap", -14.1f, -16.9f, -28.1f, -20.1f },
        { "jazzrap", -3.5f, -1.6f, -7.7f, -20.1f },     { "darkhiphop", -4.3f, -15.8f, -17.0f, -20.8f },
        { "pop", -6.8f, -16.6f, -15.1f, -19.6f },       { "electronic", -13.0f, -22.8f, -24.2f, -22.1f },
    };
    // 0.10: las plantillas «Signature 2» tienen otra forma (intro de 4 compases, versos) y otros sonidos; se calibraron
    // igual, con los paquetes 2.0 instalados (cs_render --automix <paquetes>, 2026-11-14).
    static const G table2[] = {
        { "trap", -5.8f, -16.6f, -12.6f, -15.2f },      { "reggaeton", -7.2f, -16.2f, -17.2f, -16.9f },
        { "hiphop", -6.4f, -11.3f, -20.2f, -20.1f },    { "boombap", -15.7f, -15.9f, -31.9f, -20.1f },
        { "jazzrap", -2.2f, -0.7f, -10.0f, -20.1f },    { "darkhiphop", -1.9f, -11.8f, -21.2f, -17.4f },
        { "pop", -6.6f, -15.0f, -16.3f, -17.7f },       { "electronic", -14.6f, -24.4f, -25.1f, -22.1f },
    };
    G g { "", -6.9f, -16.6f, -16.6f, -20.1f }; // medianas
    for (const auto& x : table) if (genre == x.id) g = x;
    if (style >= 2) for (const auto& x : table2) if (genre == x.id) g = x;
    if (role == "drums") return 0.0f;
    if (role == "bass") return g.bass;
    if (role == "chords") return g.chords;
    if (role == "melody") return g.melody;
    if (role == "pad") return g.pad;
    if (role == "vocal") return 1.0f;
    return g.melody;
}
} // namespace

// --- AutoMix 2 (0.10): además de los faders por papel -------------------------------------------------------------
// Todo sigue siendo DSP determinista y local (filtros y medias; nada de IA ni de servicios de pago):
//  - bombo y caja: relación entre los dos carriles del kit, por estilo;
//  - sitio para la voz: cuánto ocupa el acompañamiento la banda de la voz mientras la voz suena;
//  - choques de graves: pistas que no son el bajo ni la batería y tienen mucha energía por debajo de 150 Hz;
//  - recortes: pico de la mezcla completa con los cambios propuestos; si pasaría de -1 dBFS propone bajar el máster.
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make (bool high, double sr, double hz)
    {
        Biquad q;
        const double w = 2 * 3.14159265358979323846 * hz / sr, c = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = q.b0; }
        else { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; q.b2 = q.b0; }
        q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
        return q;
    }
    double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
struct Measure
{
    float rmsDb = -120, lowDb = -120, peak = 0;      // lowDb: por debajo de 150 Hz
    std::vector<float> frameAll, frameVoice;          // potencia por tramos de 50 ms: total y banda de la voz (300-3400 Hz)
};
float toDb (double power) { return power > 1e-14 ? (float) (10.0 * std::log10 (power)) : -120.0f; }

// Relación caja - bombo (dB, RMS de cada carril a solas) de las plantillas TAL CUAL: estilo 1 (sintetizados) y
// estilo 2 «Signature 2» con los kits 2.0. Medido con cs_render --automix. NaN = sin dato (no se propone nada).
float snareMinusKickDb (const std::string& genre, int style)
{
    struct G { const char* id; float v1, v2; };
    static const G table[] = {
        { "trap", -8.9f, -3.5f },      { "reggaeton", -9.9f, -5.1f },  { "hiphop", -7.0f, -9.2f },   { "boombap", -5.4f, -9.9f },
        { "jazzrap", -3.6f, -10.5f },  { "darkhiphop", -6.3f, -3.2f }, { "pop", -5.2f, -8.9f },      { "electronic", -8.7f, -11.4f },
    };
    for (const auto& x : table) if (genre == x.id) return style >= 2 ? x.v2 : x.v1;
    return std::nanf ("");
}
} // namespace

std::vector<AutoMixSuggestion> Session::autoMixSuggest (double maxSeconds)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    std::vector<AutoMixSuggestion> out;
    if (! open || proj.contentEnd() <= 0) return out;
    MusicPlan plan;
    const bool hasPlan = ! proj.music.empty() && MusicPlan::fromJson (proj.music, plan);
    const Frame len = std::min<Frame> (proj.contentEnd(), (Frame) (std::clamp (maxSeconds, 2.0, 120.0) * proj.sampleRate));
    const Frame hop = (Frame) (0.05 * proj.sampleRate);

    // Render offline con la misma construcción del grafo que la exportación. only >= 0: esa pista sola (los buses
    // siguen abiertos para que cuente su reverb); only < 0: la mezcla completa. lane >= 0: solo ese carril del kit.
    // gains: fader de cada pista (dB) para este render; laneDelta: cambio (dB) de los carriles 0 y 1 del kit.
    auto render = [&] (int only, int lane, const std::vector<float>& gains, const float laneDelta[2]) -> Measure
    {
        Engine offline;
        offline.prepare (proj.sampleRate, 1024);
        auto g = std::make_unique<RenderGraph>();
        g->sampleRate = proj.sampleRate; g->bpm = proj.bpm; g->beatsPerBar = proj.beatsPerBar;
        int slot = 0;
        for (size_t i = 0; i < proj.tracks.size(); ++i)
        {
            const auto& t = proj.tracks[i];
            TrackParams& p = offline.trackParams (slot);
            p.gain.store (dbToGain (gains[i])); p.pan.store (t.pan);
            p.mute.store (only >= 0 ? ((int) i != only && t.kind != "bus") : t.mute);
            p.solo.store (false);
            applySendsTo (p, t);
            TrackRT tr = makeTrackRT (t, slot++, true, *g);
            if (tr.instrument && t.kind == "drums")
                for (int ln = 0; ln < kDrumLanes; ++ln)
                {
                    const int idx = parameterIndex (*tr.instrument, "vol" + std::to_string (ln));
                    if (idx < 0) continue;
                    const auto info = tr.instrument->parameters()[(size_t) idx];
                    float v = tr.instrument->getParameter (idx);
                    if (ln < 2 && laneDelta) v += laneDelta[ln];
                    if (lane >= 0 && (int) i == only && ln != lane) v = info.minValue;
                    tr.instrument->setParameter (idx, std::clamp (v, info.minValue, info.maxValue));
                }
            g->tracks.push_back (std::move (tr));
        }
        offline.setMasterGain (1.0f);
        offline.publish (std::move (g));
        AudioData a;
        a.sampleRate = proj.sampleRate;
        a.allocate (2, len);
        offline.renderOffline (0, a.channels[0].data(), a.channels[1].data(), len);
        Measure m;
        Biquad lo1 = Biquad::make (false, proj.sampleRate, 150), lo2 = lo1;
        Biquad vh = Biquad::make (true, proj.sampleRate, 300), vl = Biquad::make (false, proj.sampleRate, 3400);
        double sum = 0, low = 0, fa = 0, fv = 0;
        for (Frame i = 0; i < len; ++i)
        {
            const double L = a.channels[0][(size_t) i], R = a.channels[1][(size_t) i], mono = 0.5 * (L + R);
            m.peak = std::max (m.peak, (float) std::max (std::fabs (L), std::fabs (R)));
            const double pw = 0.5 * (L * L + R * R);
            sum += pw;
            const double lw = lo2.run (lo1.run (mono));
            low += lw * lw;
            const double vw = vl.run (vh.run (mono));
            fa += pw; fv += vw * vw;
            if ((i + 1) % hop == 0) { m.frameAll.push_back ((float) (fa / (double) hop)); m.frameVoice.push_back ((float) (fv / (double) hop)); fa = fv = 0; }
        }
        m.rmsDb = toDb (sum / (double) std::max<Frame> (1, len));
        m.lowDb = toDb (low / (double) std::max<Frame> (1, len));
        return m;
    };
    std::vector<float> faders;
    for (const auto& t : proj.tracks) faders.push_back (t.volumeDb);

    std::vector<Measure> meas (proj.tracks.size());
    std::vector<size_t> index; // out[k] corresponde a proj.tracks[index[k]]
    for (size_t i = 0; i < proj.tracks.size(); ++i)
    {
        const auto& t = proj.tracks[i];
        if (t.kind == "bus" || t.mute) continue;
        AutoMixSuggestion s;
        s.trackId = t.id; s.name = t.name; s.volumeDb = t.volumeDb; s.pan = t.pan;
        auto it = hasPlan ? plan.roles.find (t.id) : plan.roles.end();
        s.role = it != plan.roles.end() ? it->second : t.kind == "drums" ? "drums" : t.kind == "audio" ? "vocal" : "melody";
        meas[i] = render ((int) i, -1, faders, nullptr);
        s.measuredDb = meas[i].rmsDb;
        out.push_back (s);
        index.push_back (i);
    }
    if (out.empty()) return out;
    // Referencia: la batería; si no hay, la pista más fuerte (se le asigna objetivo 0).
    float ref = -120.0f;
    for (const auto& s : out) if (s.role == "drums") ref = std::max (ref, s.measuredDb);
    if (ref <= -119.0f) for (const auto& s : out) ref = std::max (ref, s.measuredDb - roleOffsetDb (plan.genre, s.role, plan.style));
    for (auto& s : out)
    {
        s.suggestedVolumeDb = s.volumeDb;
        s.suggestedPan = s.pan;
        if (s.measuredDb <= -90.0f) { s.reason = "sin sonido en el tramo analizado"; continue; }
        s.referenceDb = ref;
        s.targetDb = ref + roleOffsetDb (plan.genre, s.role, plan.style);
        const float delta = std::clamp (s.targetDb - s.measuredDb, -6.0f, 6.0f);
        if (std::fabs (delta) >= 1.5f)
        {
            s.suggestedVolumeDb = std::clamp (s.volumeDb + delta, -40.0f, 6.0f);
            s.reason = delta > 0 ? "queda por debajo de lo esperado para su papel" : "tapa a las demás para su papel";
        }
    }

    // --- Sitio para la voz ---------------------------------------------------------------------------------------
    // Mientras la voz suena, el acompañamiento (acordes, colchón, melodía) no debería ocupar su banda (300-3400 Hz)
    // a menos de 6 dB de ella. Umbral por convención, SIN calibrar con voces reales. Se baja como mucho 3 dB la pista
    // que más ocupa, encima de lo que ya proponga la regla de su papel (total entre -6 y +6 dB).
    {
        int vocal = -1;
        for (size_t k = 0; k < out.size(); ++k) if (out[k].role == "vocal" && out[k].measuredDb > -90.0f && (vocal < 0 || out[k].measuredDb > out[(size_t) vocal].measuredDb)) vocal = (int) k;
        if (vocal >= 0)
        {
            const Measure& v = meas[index[(size_t) vocal]];
            float top = 0;
            for (float x : v.frameAll) top = std::max (top, x);
            double vp = 0; int frames = 0;
            std::vector<double> acc (out.size(), 0.0);
            for (size_t f = 0; f < v.frameAll.size(); ++f)
            {
                if (v.frameAll[f] < top * 0.003f) continue; // la voz calla (25 dB por debajo de su máximo)
                ++frames;
                vp += v.frameVoice[f];
                for (size_t k = 0; k < out.size(); ++k)
                {
                    if ((int) k == vocal || (out[k].role != "chords" && out[k].role != "pad" && out[k].role != "melody")) continue;
                    const Measure& m = meas[index[k]];
                    const double gain = std::pow (10.0, (out[k].suggestedVolumeDb - out[k].volumeDb) / 10.0); // con lo ya propuesto
                    if (f < m.frameVoice.size()) acc[k] += m.frameVoice[f] * gain;
                }
            }
            double total = 0; size_t worst = out.size();
            for (size_t k = 0; k < out.size(); ++k) { total += acc[k]; if (acc[k] > 0 && (worst == out.size() || acc[k] > acc[worst])) worst = k; }
            if (frames > 0 && vp > 0 && worst < out.size())
            {
                const float margin = toDb (vp / frames) - toDb (total / frames); // voz - acompañamiento en la banda de la voz
                if (margin < 6.0f)
                {
                    auto& s = out[worst];
                    const float cut = std::min (3.0f, 6.0f - margin);
                    s.suggestedVolumeDb = std::clamp (std::max (s.volumeDb - 6.0f, s.suggestedVolumeDb - cut), -40.0f, 6.0f);
                    s.reason = (s.reason.empty() ? std::string() : s.reason + "; ") + "ocupa la banda de la voz mientras se canta: bajarla le deja sitio";
                }
            }
        }
    }

    // --- Choques de graves (solo aviso: AutoMix no toca ecualizadores) --------------------------------------------
    {
        float bassLow = -120;
        for (size_t k = 0; k < out.size(); ++k) if (out[k].role == "bass") bassLow = std::max (bassLow, meas[index[k]].lowDb);
        for (size_t k = 0; k < out.size(); ++k)
        {
            auto& s = out[k];
            if (s.role == "bass" || s.role == "drums" || s.measuredDb <= -90.0f) continue;
            const Measure& m = meas[index[k]];
            // Más de un tercio de su energía por debajo de 150 Hz y a menos de 10 dB de los graves del bajo.
            if (bassLow > -90.0f && m.lowDb > m.rmsDb - 4.8f && m.lowDb > bassLow - 10.0f)
                s.reason = (s.reason.empty() ? std::string() : s.reason + "; ") + "tiene graves que compiten con el bajo: prueba el paso alto de su EQ";
        }
    }
    for (auto& s : out)
    {
        s.suggestedVolumeDb = std::round (s.suggestedVolumeDb * 10.0f) / 10.0f;
        s.change = std::fabs (s.suggestedVolumeDb - s.volumeDb) >= 0.05f || std::fabs (s.suggestedPan - s.pan) >= 0.01f;
        if (! s.change && s.reason.empty()) s.reason = "ya está en su sitio";
    }

    // --- Bombo y caja ---------------------------------------------------------------------------------------------
    float laneDelta[2] = { 0, 0 };
    for (size_t i = 0; i < proj.tracks.size(); ++i)
    {
        const auto& t = proj.tracks[i];
        if (t.kind != "drums" || t.mute || t.instrument.type != "cs.drums") continue;
        const Measure kick = render ((int) i, 0, faders, nullptr), snare = render ((int) i, 1, faders, nullptr);
        const json st = json::parse (t.instrument.state.empty() ? std::string ("{}") : t.instrument.state, nullptr, false);
        // La referencia solo vale para el kit con el que se calibró: sintetizado en las plantillas de siempre, Drums II
        // en «Signature 2». Con otro kit no se propone nada (no hay con qué comparar).
        const std::string sampleKit = st.is_object() ? st.value ("sampleKit", std::string()) : std::string();
        const bool known = plan.style >= 2 ? sampleKit.rfind ("cauce.drums.two:", 0) == 0 : sampleKit.empty();
        const float target = known ? snareMinusKickDb (plan.genre, plan.style) : std::nanf ("");
        const float snareVol = st.is_object() ? st.value ("vol1", 0.0f) : 0.0f;
        AutoMixSuggestion s;
        s.trackId = t.id + "#snare"; s.name = t.name + ": caja"; s.role = "snare";
        s.volumeDb = s.suggestedVolumeDb = snareVol;
        s.measuredDb = snare.rmsDb;
        s.referenceDb = kick.rmsDb;
        if (kick.rmsDb <= -90.0f || snare.rmsDb <= -90.0f) s.reason = "sin bombo o sin caja en el tramo analizado";
        else if (std::isnan (target)) s.reason = "sin referencia para este kit o estilo";
        else
        {
            s.targetDb = kick.rmsDb + target;
            const float delta = std::clamp (s.targetDb - s.measuredDb, -4.0f, 4.0f);
            if (std::fabs (delta) >= 2.0f)
            {
                s.suggestedVolumeDb = std::round (std::clamp (snareVol + delta, -24.0f, 6.0f) * 10.0f) / 10.0f;
                s.reason = delta > 0 ? "la caja queda escondida detrás del bombo para este estilo" : "la caja pega demasiado frente al bombo para este estilo";
                laneDelta[1] = s.suggestedVolumeDb - snareVol;
            }
            else s.reason = "bombo y caja equilibrados para este estilo";
        }
        s.change = std::fabs (s.suggestedVolumeDb - s.volumeDb) >= 0.05f;
        out.push_back (s);
        break; // un kit por proyecto
    }

    // --- Recortes: la mezcla completa con todo lo propuesto ------------------------------------------------------
    {
        std::vector<float> after = faders;
        for (size_t k = 0; k < index.size(); ++k) after[index[k]] = out[k].suggestedVolumeDb;
        const Measure mix = render (-1, -1, after, laneDelta);
        const float peakDb = toDb ((double) mix.peak * mix.peak) + proj.masterDb;
        AutoMixSuggestion s;
        s.trackId = "@master"; s.name = "Máster"; s.role = "master";
        s.volumeDb = s.suggestedVolumeDb = proj.masterDb;
        s.measuredDb = peakDb; // aquí es el PICO de la mezcla (dBFS), no un RMS
        s.targetDb = -1.0f;
        if (peakDb > -1.0f)
        {
            s.suggestedVolumeDb = std::floor ((proj.masterDb - (peakDb + 1.0f)) * 2.0f) / 2.0f; // hacia abajo, en pasos de 0,5 dB
            s.reason = "la mezcla recortaría: el pico llega a " + std::to_string ((int) std::lround (peakDb * 10) / 10) + " dBFS; bajar el máster lo deja en -1";
            s.change = true;
        }
        else s.reason = "sin recortes: el pico queda por debajo de -1 dBFS";
        out.push_back (s);
    }
    return out;
}

// Los cambios llevan el id de una pista, "<id de la batería>#snare" (volumen del carril de caja del kit) o "@master".
namespace
{
bool laneTarget (const std::string& id, std::string& trackId, std::string& param)
{
    const size_t h = id.find ('#');
    if (h == std::string::npos) return false;
    trackId = id.substr (0, h);
    param = id.substr (h + 1) == "snare" ? "vol1" : id.substr (h + 1) == "kick" ? "vol0" : "";
    return ! param.empty();
}
} // namespace

float Session::autoMixValueLocked (const std::string& id, bool& ok)
{
    ok = true;
    std::string tid, param;
    if (id == "@master") return proj.masterDb;
    if (laneTarget (id, tid, param))
    {
        const TrackModel* t = proj.findTrack (tid);
        if (! t || t->instrument.type != "cs.drums") { ok = false; return 0; }
        const json st = json::parse (t->instrument.state.empty() ? std::string ("{}") : t->instrument.state, nullptr, false);
        return st.is_object() ? st.value (param, 0.0f) : 0.0f;
    }
    const TrackModel* t = proj.findTrack (id);
    ok = t != nullptr;
    return t ? t->volumeDb : 0;
}

void Session::autoMixSetLocked (const MixChange& c)
{
    std::string tid, param;
    if (c.trackId == "@master") { proj.masterDb = std::clamp (c.volumeDb, -144.0f, 12.0f); return; } // applyMixerParams lo lleva al motor
    if (laneTarget (c.trackId, tid, param))
    {
        TrackModel* t = proj.findTrack (tid);
        if (! t) return;
        json st = json::parse (t->instrument.state.empty() ? std::string ("{}") : t->instrument.state, nullptr, false);
        if (! st.is_object()) st = json::object();
        st[param] = c.volumeDb;
        t->instrument.state = st.dump();
        auto it = instrumentInstances.find (t->id);
        if (it != instrumentInstances.end() && it->second)
        {
            const int idx = parameterIndex (*it->second, param);
            if (idx >= 0) { it->second->setParameter (idx, c.volumeDb); t->instrument.state = it->second->saveState(); } // el instrumento valida el rango
        }
        return;
    }
    if (TrackModel* t = proj.findTrack (c.trackId)) { t->volumeDb = std::clamp (c.volumeDb, -144.0f, 12.0f); t->pan = std::clamp (c.pan, -1.0f, 1.0f); }
}

OpResult Session::applyAutoMix (const std::vector<MixChange>& changes)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    for (const auto& c : changes)
    {
        bool ok = false;
        autoMixValueLocked (c.trackId, ok);
        if (! ok || ! std::isfinite (c.volumeDb) || ! std::isfinite (c.pan)) return OpResult::fail ("cambio no válido");
    }
    if (changes.empty()) return {};
    autoMixBefore.clear();
    autoMixShowsBefore = false;
    for (const auto& c : changes)
    {
        bool ok = false;
        const float was = autoMixValueLocked (c.trackId, ok);
        const TrackModel* t = proj.findTrack (c.trackId);
        autoMixBefore.push_back ({ c.trackId, was, t ? t->pan : 0.0f });
        autoMixSetLocked (c);
    }
    touchLocked();
    applyMixerParams();
    return {};
}
// Alterna entre la mezcla anterior (A) y la de AutoMix (B) sin perder ninguna: sirve para comparar de oído.
void Session::swapAutoMixLocked()
{
    std::vector<MixChange> other;
    for (const auto& c : autoMixBefore)
    {
        bool ok = false;
        const float now = autoMixValueLocked (c.trackId, ok);
        if (! ok) continue;
        const TrackModel* t = proj.findTrack (c.trackId);
        other.push_back ({ c.trackId, now, t ? t->pan : 0.0f });
        autoMixSetLocked (c);
    }
    autoMixBefore = std::move (other);
    autoMixShowsBefore = ! autoMixShowsBefore;
    touchLocked();
    applyMixerParams();
}

OpResult Session::compareAutoMix()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open || autoMixBefore.empty()) return OpResult::fail ("no hay AutoMix que comparar");
    swapAutoMixLocked();
    OpResult r;
    r.id = autoMixShowsBefore ? "before" : "automix";
    return r;
}

// Deja la mezcla anterior y olvida la de AutoMix.
OpResult Session::revertAutoMix()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open || autoMixBefore.empty()) return OpResult::fail ("no hay AutoMix que revertir");
    if (! autoMixShowsBefore) swapAutoMixLocked();
    autoMixBefore.clear();
    autoMixShowsBefore = false;
    return {};
}

bool Session::canRevertAutoMix() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    return open && ! autoMixBefore.empty();
}
} // namespace cs
