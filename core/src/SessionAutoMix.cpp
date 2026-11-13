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

#include <algorithm>
#include <cmath>

namespace cs
{
namespace
{
// Objetivos por estilo calibrados con las plantillas TAL CUAL (balance 0.6 aprobado por Diego): `cs_render --automix`
// mide cada papel respecto a la batería en los primeros 16 s. Así una plantilla sin tocar no recibe sugerencias y AutoMix
// solo corrige lo que el usuario desequilibró. Sin estilo: la mediana de las ocho plantillas. Voz: +1 dB sobre la batería
// (convención habitual, NO calibrada con grabaciones reales: pendiente de validar con voces de verdad).
float roleOffsetDb (const std::string& genre, const std::string& role)
{
    struct G { const char* id; float bass, chords, melody, pad; };
    static const G table[] = {
        { "trap", -6.1f, -16.6f, -11.6f, -17.3f },      { "reggaeton", -7.3f, -17.3f, -16.3f, -20.1f },
        { "hiphop", -7.0f, -12.0f, -18.8f, -20.1f },    { "boombap", -14.1f, -16.9f, -28.1f, -20.1f },
        { "jazzrap", -3.5f, -1.6f, -7.7f, -20.1f },     { "darkhiphop", -4.3f, -15.8f, -17.0f, -20.8f },
        { "pop", -6.8f, -16.6f, -15.1f, -19.6f },       { "electronic", -13.0f, -22.8f, -24.2f, -22.1f },
    };
    G g { "", -6.9f, -16.6f, -16.6f, -20.1f }; // medianas
    for (const auto& x : table) if (genre == x.id) g = x;
    if (role == "drums") return 0.0f;
    if (role == "bass") return g.bass;
    if (role == "chords") return g.chords;
    if (role == "melody") return g.melody;
    if (role == "pad") return g.pad;
    if (role == "vocal") return 1.0f;
    return g.melody;
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

    // Render de una pista sola: misma construcción del grafo que la exportación, con las demás en silencio.
    auto measure = [&] (size_t only) -> float
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
            p.gain.store (dbToGain (t.volumeDb)); p.pan.store (t.pan); p.mute.store (i != only && t.kind != "bus"); p.solo.store (false);
            applySendsTo (p, t);
            g->tracks.push_back (makeTrackRT (t, slot++, true, *g));
        }
        offline.setMasterGain (1.0f);
        offline.publish (std::move (g));
        AudioData a;
        a.sampleRate = proj.sampleRate;
        a.allocate (2, len);
        offline.renderOffline (0, a.channels[0].data(), a.channels[1].data(), len);
        double sum = 0;
        for (Frame i = 0; i < len; ++i) sum += 0.5 * ((double) a.channels[0][(size_t) i] * a.channels[0][(size_t) i] + (double) a.channels[1][(size_t) i] * a.channels[1][(size_t) i]);
        const double rms = std::sqrt (sum / std::max<Frame> (1, len));
        return rms > 1e-7 ? (float) (20.0 * std::log10 (rms)) : -120.0f;
    };

    for (size_t i = 0; i < proj.tracks.size(); ++i)
    {
        const auto& t = proj.tracks[i];
        if (t.kind == "bus" || t.mute) continue;
        AutoMixSuggestion s;
        s.trackId = t.id; s.name = t.name; s.volumeDb = t.volumeDb; s.pan = t.pan;
        auto it = hasPlan ? plan.roles.find (t.id) : plan.roles.end();
        s.role = it != plan.roles.end() ? it->second : t.kind == "drums" ? "drums" : t.kind == "audio" ? "vocal" : "melody";
        s.measuredDb = measure (i);
        out.push_back (s);
    }
    if (out.empty()) return out;
    // Referencia: la batería; si no hay, la pista más fuerte (se le asigna objetivo 0).
    float ref = -120.0f;
    for (const auto& s : out) if (s.role == "drums") ref = std::max (ref, s.measuredDb);
    if (ref <= -119.0f) for (const auto& s : out) ref = std::max (ref, s.measuredDb - roleOffsetDb (plan.genre, s.role));
    for (auto& s : out)
    {
        s.suggestedVolumeDb = s.volumeDb;
        s.suggestedPan = s.pan;
        if (s.measuredDb <= -90.0f) { s.reason = "sin sonido en el tramo analizado"; continue; }
        s.targetDb = ref + roleOffsetDb (plan.genre, s.role);
        const float delta = std::clamp (s.targetDb - s.measuredDb, -6.0f, 6.0f);
        if (std::fabs (delta) >= 1.5f)
        {
            s.suggestedVolumeDb = std::clamp (s.volumeDb + delta, -40.0f, 6.0f);
            s.reason = delta > 0 ? "queda por debajo de lo esperado para su papel" : "tapa a las demás para su papel";
        }
        s.suggestedVolumeDb = std::round (s.suggestedVolumeDb * 10.0f) / 10.0f;
        s.change = std::fabs (s.suggestedVolumeDb - s.volumeDb) >= 0.05f || std::fabs (s.suggestedPan - s.pan) >= 0.01f;
        if (! s.change && s.reason.empty()) s.reason = "ya está en su sitio";
    }
    return out;
}

OpResult Session::applyAutoMix (const std::vector<MixChange>& changes)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    for (const auto& c : changes)
        if (! proj.findTrack (c.trackId) || ! std::isfinite (c.volumeDb) || ! std::isfinite (c.pan)) return OpResult::fail ("cambio no válido");
    if (changes.empty()) return {};
    autoMixBefore.clear();
    for (const auto& c : changes)
    {
        TrackModel* t = proj.findTrack (c.trackId);
        autoMixBefore.push_back ({ t->id, t->volumeDb, t->pan });
        t->volumeDb = std::clamp (c.volumeDb, -144.0f, 12.0f);
        t->pan = std::clamp (c.pan, -1.0f, 1.0f);
    }
    touchLocked();
    applyMixerParams();
    return {};
}
OpResult Session::revertAutoMix()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open || autoMixBefore.empty()) return OpResult::fail ("no hay AutoMix que revertir");
    for (const auto& c : autoMixBefore)
        if (TrackModel* t = proj.findTrack (c.trackId)) { t->volumeDb = c.volumeDb; t->pan = c.pan; }
    autoMixBefore.clear();
    touchLocked();
    applyMixerParams();
    return {};
}

bool Session::canRevertAutoMix() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    return open && ! autoMixBefore.empty();
}
} // namespace cs
