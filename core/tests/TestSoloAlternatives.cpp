// Misión 0.7 (Channel Rack & Mixer Pro), prioridad 1: las cuatro alternativas A-D de Solo AI.
// Reproduce exactamente la secuencia de comandos de la app (soloProposals + soloMelody con replaceTrackId) y mide,
// para cada alternativa, el MIDI que queda en la pista y el audio que sale del motor EN VIVO (Engine::process, el
// mismo camino que Oboe) y en la exportación. Si CS_SOLO_REPORT_DIR está definido, escribe allí el informe JSON,
// un .mid y un .wav por alternativa.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Smf.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <set>

using namespace cs;
using nlohmann::json;

#ifdef CS_WITH_SOLO
namespace
{
struct Fx
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 512); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
};

const TrackModel* soloTrack (const ProjectModel& m, const std::string& id)
{
    for (const auto& t : m.tracks) if (t.id == id) return &t;
    return nullptr;
}

uint64_t notesHash (const std::vector<MidiNote>& v)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (int64_t x) { for (int i = 0; i < 8; ++i) { h ^= (uint64_t) ((x >> (i * 8)) & 0xff); h *= 1099511628211ull; } };
    for (const auto& n : v) { mix (n.key); mix (n.velocity); mix (n.start); mix (n.length); }
    return h;
}

// Audio en vivo: transporte desde 'from' durante 'frames', bloques de 192 (como un teléfono). Mono L+R.
std::vector<float> renderLive (Engine& e, Frame from, Frame frames)
{
    e.stop();
    std::vector<float> out ((size_t) 192 * 2), mono;
    for (int i = 0; i < 8; ++i) e.process (nullptr, 0, out.data(), 2, 192); // adopta el grafo y deja sonar las colas en parado
    e.seek (from);
    e.play();
    mono.reserve ((size_t) frames);
    for (Frame done = 0; done < frames; done += 192)
    {
        e.process (nullptr, 0, out.data(), 2, 192);
        for (int i = 0; i < 192; ++i) mono.push_back (0.5f * (out[(size_t) i * 2] + out[(size_t) i * 2 + 1]));
    }
    e.stop();
    for (int i = 0; i < 64; ++i) e.process (nullptr, 0, out.data(), 2, 192);
    return mono;
}

double rms (const std::vector<float>& a) { double s = 0; for (float x : a) s += (double) x * x; return std::sqrt (s / std::max<size_t> (1, a.size())); }
double diffRatio (const std::vector<float>& a, const std::vector<float>& b)
{
    double s = 0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { const double d = (double) a[i] - b[i]; s += d * d; }
    return std::sqrt (s / std::max<size_t> (1, n)) / std::max (1e-9, 0.5 * (rms (a) + rms (b)));
}

// Envolvente en dB por ventanas de 10 ms: no depende de la fase de los osciladores (que el sintetizador conserva entre
// reproducciones), así que dos escuchas del mismo MIDI dan casi la misma envolvente y dos MIDI distintos no.
std::vector<double> envelope (const std::vector<float>& a)
{
    std::vector<double> e;
    for (size_t i = 0; i + 480 <= a.size(); i += 480)
    {
        double s = 0;
        for (size_t k = i; k < i + 480; ++k) s += (double) a[k] * a[k];
        e.push_back (10 * std::log10 (s / 480 + 1e-10));
    }
    return e;
}
double envelopeDistanceDb (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto x = envelope (a), y = envelope (b);
    double s = 0; size_t n = std::min (x.size(), y.size());
    for (size_t i = 0; i < n; ++i) s += std::fabs (std::max (x[i], -60.0) - std::max (y[i], -60.0));
    return n ? s / n : 0;
}

// Huella rítmica: posiciones de semicorchea con ataque dentro del compás (16 bits por compás, OR de todos).
std::string rhythmString (const std::vector<MidiNote>& v, int bars)
{
    std::string s ((size_t) bars * 16, '.');
    for (const auto& n : v) { const long st = (long) (n.start / (kPpq / 4)); if (st >= 0 && st < (long) s.size()) s[(size_t) st] = 'x'; }
    return s;
}
} // namespace

TEST (solo_ai_alternatives_reach_the_track_and_the_audio_as_the_app_selects_them)
{
    Fx f ("solo_alt_app");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "trap").ok);
    // Exactamente lo que envía la app (SoloAiPanel → Commands.solo): sin keyRoot (plan de la plantilla), preset.
    const json base = { { "startBar", 2 }, { "bars", 8 }, { "type", "chorus" }, { "character", "easy" }, { "freedom", 35 },
                        { "seed", 1234567 }, { "minor", true }, { "preset", "bright_lead" } };
    json q = base; q["cmd"] = "soloProposals";
    const json fam = f.cmd (q);
    CHECK_MSG (fam.value ("ok", false), fam.dump());
    q = base; q["cmd"] = "soloMelody"; q["proposal"] = 0;
    const json a = f.cmd (q);
    CHECK_MSG (a.value ("ok", false), a.dump());
    const std::string trk = a.value ("id", std::string());
    const ProjectModel m0 = f.session.model();
    const auto* t0 = soloTrack (m0, trk);
    CHECK (t0 != nullptr);
    if (! t0) return;
    const Frame from = m0.ticksToFrames ((Tick) 2 * 4 * kPpq), len = m0.ticksToFrames ((Tick) 8 * 4 * kPpq);
    CHECK (f.session.setTrackSolo (trk, true).ok); // escuchar la melodía aislada

    const char* reportDir = std::getenv ("CS_SOLO_REPORT_DIR");
    json report = json::array();
    std::vector<std::vector<MidiNote>> midi;
    std::vector<std::vector<float>> audio;
    const int order[] = { 0, 1, 2, 3, 0 }; // A→B→C→D→A
    for (int step = 0; step < 5; ++step)
    {
        const int p = order[step];
        if (step > 0)
        {
            q = base; q["cmd"] = "soloMelody"; q["proposal"] = p; q["replaceTrackId"] = trk;
            const json r = f.cmd (q);
            CHECK_MSG (r.value ("ok", false) && r.value ("id", std::string()) == trk, r.dump());
        }
        const ProjectModel m = f.session.model();
        const auto* t = soloTrack (m, trk);
        CHECK (t && t->midiClips.size() == 1);
        if (! t) return;
        const auto& notes = t->midiClips.front().notes;
        CHECK ((int) notes.size() == fam["proposals"][(size_t) p].value ("notes", -1)); // lo anunciado es lo que entra
        midi.push_back (notes);
        audio.push_back (renderLive (f.engine, from, len));
        CHECK (rms (audio.back()) > 1e-3); // la pista suena
        if (step < 4)
        {
            json notesJ = json::array();
            for (const auto& n : notes) notesJ.push_back ({ n.key, n.start, n.length, n.velocity });
            report.push_back ({ { "alternative", std::string (1, (char) ('A' + p)) }, { "name", fam["proposals"][(size_t) p].value ("name", std::string()) },
                                { "seed", base["seed"] }, { "proposal", p }, { "notes", notes.size() }, { "midiHash", notesHash (notes) },
                                { "rhythm", rhythmString (notes, 8) }, { "liveRms", rms (audio.back()) }, { "notesKeyStartLengthVelocity", notesJ } });
            if (reportDir)
            {
                const std::string name = std::string (reportDir) + "/alternativa_" + (char) ('A' + p);
                CHECK (f.session.exportMidi (name + ".mid").ok);
                AudioData ad; ad.sampleRate = 48000; ad.channels = { audio.back() };
                std::string err; CHECK (writeWav (name + "_vivo.wav", ad, WavFormat::Pcm24, err));
            }
        }
    }
    // MIDI distinto entre las cuatro y el audio en vivo cambia con él: si la selección no llegara al motor, el
    // audio de B, C o D sería igual al de A.
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
        {
            CHECK_MSG (notesHash (midi[(size_t) i]) != notesHash (midi[(size_t) j]), std::string ("MIDI idéntico ") + (char) ('A' + i) + (char) ('A' + j));
            const double d = envelopeDistanceDb (audio[(size_t) i], audio[(size_t) j]);
            MEASURE ("audio en vivo %c vs %c: distancia media de envolvente %.1f dB", 'A' + i, 'A' + j, d);
            CHECK (d > 4.0);
        }
    // Volver a A da exactamente el mismo MIDI y el mismo audio (no hay caché ni estado arrastrado).
    CHECK (notesHash (midi[4]) == notesHash (midi[0]));
    const double back = envelopeDistanceDb (audio[4], audio[0]);
    MEASURE ("audio en vivo A vs A (tras pasar por B, C y D): %.2f dB", back);
    CHECK (back < 1.0);
    // Lo mismo con la canción completa sonando (como lo escucha el usuario): ¿cuánto pesa la melodía en la mezcla?
    CHECK (f.session.setTrackSolo (trk, false).ok);
    std::vector<std::vector<float>> full;
    for (int p = 0; p < 4; ++p)
    {
        q = base; q["cmd"] = "soloMelody"; q["proposal"] = p; q["replaceTrackId"] = trk;
        CHECK (f.cmd (q).value ("ok", false));
        full.push_back (renderLive (f.engine, from, len));
    }
    CHECK (f.session.setTrackMute (trk, true).ok);
    const auto without = renderLive (f.engine, from, len);
    CHECK (f.session.setTrackMute (trk, false).ok);
    MEASURE ("mezcla completa: RMS %.4f; sin la melodía de Solo AI %.4f; la melodía sola %.4f (%.1f dB bajo la mezcla)", rms (full[0]), rms (without), rms (audio[0]),
             20 * std::log10 (rms (audio[0]) / std::max (1e-9, rms (full[0]))));
    for (int i = 1; i < 4; ++i) MEASURE ("mezcla completa A vs %c: diferencia relativa %.2f", 'A' + i, diffRatio (full[0], full[(size_t) i]));
    if (reportDir) { std::string err; atomicWriteFile (std::string (reportDir) + "/alternativas.json", report.dump (2), err, false); }
}
#endif

#ifdef CS_WITH_SOLO
// Los perfiles tienen identidad medible y respetan la armonía, con varias tonalidades, tipos y semillas.
TEST (solo_ai_profiles_have_measurable_identity_and_keep_the_harmony)
{
    struct Case { int root; bool minor; const char* type; const char* character; uint64_t seed; };
    const Case cases[] = { { 9, true, "chorus", "easy", 1 }, { 4, true, "chorus", "easy", 77 }, { 7, false, "chorus", "emotional", 5 },
                           { 2, true, "chorus", "energetic", 12345 }, { 0, false, "solo", "easy", 9 }, { 5, true, "interlude", "easy", 31 } };
    int idx = 0;
    double worstOverlap = 0;
    for (const auto& c : cases)
    {
        solo::MelodyRequest r;
        r.keyRoot = c.root; r.minor = c.minor; r.bpm = 120; r.type = c.type; r.character = c.character; r.seed = c.seed;
        static const int dMin[4] = { 0, 8, 5, 7 }, qMin[4] = { 1, 0, 1, 1 }, dMaj[4] = { 0, 9, 5, 7 }, qMaj[4] = { 0, 1, 0, 0 };
        for (int b = 0; b < 8; ++b) r.chords.push_back ({ (c.root + (c.minor ? dMin : dMaj)[b % 4]) % 12, (c.minor ? qMin : qMaj)[b % 4] });
        const auto fam = solo::proposeFamily (r);
        CHECK_MSG (fam.ok, fam.error);
        if (! fam.ok) continue;
        solo::ProfileMetrics m[4];
        for (int i = 0; i < 4; ++i)
        {
            m[i] = solo::measureProfile (fam.proposals[(size_t) i].notes, r);
            CHECK (m[i].inScale >= 0.95);
            CHECK_MSG (m[i].strongChordTones >= 0.8, std::to_string (m[i].strongChordTones));
            CHECK (m[i].highKey - m[i].lowKey <= 31 && m[i].lowKey >= 36 && m[i].highKey <= 96);
            // Reproducible: la misma petición con proposal = i da exactamente las mismas notas.
            solo::MelodyRequest one = r; one.proposal = i;
            const auto g = solo::generateMelody (one);
            CHECK (g.ok && solo::onsetOverlap (g.notes, fam.proposals[(size_t) i].notes) == 1.0 && g.notes.size() == fam.proposals[(size_t) i].notes.size());
        }
        MEASURE ("caso %d (%s/%s): notas/compás A %.1f B %.1f C %.1f D %.1f | duración media (tiempos) A %.2f B %.2f C %.2f D %.2f | síncopa A %.2f B %.2f C %.2f D %.2f | salto medio A %.1f B %.1f C %.1f D %.1f | altura media A %.0f B %.0f C %.0f D %.0f",
                 idx, c.type, c.character, m[0].notesPerBar, m[1].notesPerBar, m[2].notesPerBar, m[3].notesPerBar,
                 m[0].meanLengthBeats, m[1].meanLengthBeats, m[2].meanLengthBeats, m[3].meanLengthBeats,
                 m[0].syncopation, m[1].syncopation, m[2].syncopation, m[3].syncopation,
                 m[0].meanLeap, m[1].meanLeap, m[2].meanLeap, m[3].meanLeap, m[0].meanPitch, m[1].meanPitch, m[2].meanPitch, m[3].meanPitch);
        // Identidad de cada perfil frente a los demás (la propiedad que lo define, no su nombre):
        CHECK (m[0].notesPerBar <= m[2].notesPerBar && m[0].notesPerBar <= m[3].notesPerBar);      // A: menos notas que C y D
        CHECK (m[1].meanLengthBeats > m[0].meanLengthBeats && m[1].meanLengthBeats > m[2].meanLengthBeats && m[1].meanLengthBeats > m[3].meanLengthBeats); // B: notas más largas
        CHECK (m[2].syncopation > m[0].syncopation && m[2].syncopation > m[1].syncopation);         // C: más síncopa
        CHECK (m[2].meanPitch < m[0].meanPitch && m[2].meanPitch < m[1].meanPitch && m[2].meanPitch < m[3].meanPitch); // C: registro grave
        CHECK (m[3].meanLeap > m[0].meanLeap && m[3].meanLeap > m[1].meanLeap);                   // D: saltos mayores
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
            {
                const double ov = solo::onsetOverlap (fam.proposals[(size_t) i].notes, fam.proposals[(size_t) j].notes);
                worstOverlap = std::max (worstOverlap, ov);
                CHECK_MSG (ov < 0.35, std::string ("demasiado parecidas ") + (char) ('A' + i) + (char) ('A' + j) + " " + std::to_string (ov));
            }
        ++idx;
    }
    MEASURE ("solapamiento máximo de notas entre dos alternativas en %d casos: %.2f (1 = idénticas)", idx, worstOverlap);
}

// Cambiar de alternativa con el transporte en marcha no deja notas colgadas ni toca las demás pistas; guardar y
// reabrir conserva la elegida; deshacer vuelve a la anterior.
TEST (solo_ai_switching_while_playing_has_no_stuck_notes_and_survives_save_reopen_undo)
{
    Fx f ("solo_alt_play");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "pop").ok);
    const json base = { { "startBar", 2 }, { "bars", 8 }, { "seed", 99 }, { "minor", false }, { "preset", "bright_lead" } };
    json q = base; q["cmd"] = "soloMelody"; q["proposal"] = 0;
    const json a = f.cmd (q);
    CHECK_MSG (a.value ("ok", false), a.dump());
    const std::string trk = a.value ("id", std::string());
    std::vector<float> buf (192 * 2);
    f.engine.seek (f.session.model().ticksToFrames ((Tick) 2 * 4 * kPpq));
    f.engine.play();
    for (int step = 1; step <= 12; ++step)
    {
        for (int i = 0; i < 40; ++i) f.engine.process (nullptr, 0, buf.data(), 2, 192); // ~160 ms sonando
        q = base; q["cmd"] = "soloMelody"; q["proposal"] = step % 4; q["replaceTrackId"] = trk;
        CHECK (f.cmd (q).value ("ok", false));
        CHECK (f.session.activeVoices (trk) <= 8);
    }
    f.engine.stop();
    for (int i = 0; i < 2000; ++i) f.engine.process (nullptr, 0, buf.data(), 2, 192); // 8 s: release y colas de reverb
    CHECK_MSG (f.session.activeVoices (trk) == 0, std::to_string (f.session.activeVoices (trk)));
    float peak = 0;
    for (float x : buf) peak = std::max (peak, std::fabs (x));
    CHECK (peak < 1e-4); // silencio: nada colgado
    // La última elegida fue la 0 (12 % 4): undo vuelve a D (11 % 4 = 3).
    const auto chosen = f.session.model().findTrack (trk)->midiClips.front().notes;
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const auto reopened = f.session.model();
    const auto* t = reopened.findTrack (trk);
    if (t) MEASURE ("guardado %zu notas, reabierto %zu, solapamiento %.2f", chosen.size(), t->midiClips.front().notes.size(), solo::onsetOverlap (t->midiClips.front().notes, chosen));
    CHECK (t && solo::onsetOverlap (t->midiClips.front().notes, chosen) == 1.0);
    CHECK (t && t->name.find ("Sencilla") != std::string::npos);
    q = base; q["cmd"] = "soloMelody"; q["proposal"] = 3; q["replaceTrackId"] = trk;
    CHECK (f.cmd (q).value ("ok", false));
    CHECK (f.session.undo().ok);
    CHECK (solo::onsetOverlap (f.session.model().findTrack (trk)->midiClips.front().notes, chosen) == 1.0);
}
#endif
