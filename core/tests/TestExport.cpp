// Misión 0.6: exportación para DAW (stems + MIDI + paquete ZIP con manifiesto).
// Lo que estas pruebas demuestran con archivos reales escritos en disco y vueltos a leer:
//  - el paquete contiene lo que el manifiesto dice, con hashes SHA-256 que coinciden;
//  - todos los WAV empiezan en el mismo punto, duran lo mismo y tienen la misma frecuencia;
//  - en modo «con efectos» la suma de los stems es la mezcla (antes del master): no se pierde ni se duplica el bus;
//  - en modo «sin efectos» cada pista sale limpia y no hay retornos;
//  - el MIDI se vuelve a leer con las mismas notas, en la misma posición, con tempo, compás y nombres;
//  - exportar no modifica el proyecto, y cancelar o cerrar a mitad no deja archivos a medias.
// Lo que NO demuestran: que un DAW concreto (FL Studio, Reaper…) lo importe bien. Eso se comprueba a mano.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Archive.h"
#include "cs/Hash.h"
#include "cs/Session.h"
#include "cs/Smf.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <thread>

using namespace cs;
using nlohmann::json;

namespace
{
constexpr int kSr = 48000;

struct Fx
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (kSr, 512); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
};

json waitJob (Fx& f, const std::string& jobId)
{
    for (int i = 0; i < 120000; ++i)
    {
        const json j = f.cmd ({ { "cmd", "jobStatus" }, { "jobId", jobId } });
        const std::string st = j.value ("state", std::string());
        if (st == "done" || st == "failed" || st == "cancelled") return j;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return {};
}

std::string toneWav (const std::string& path, double seconds, double hz, float level = 0.3f)
{
    AudioData a;
    a.sampleRate = kSr;
    a.allocate (1, (Frame) (seconds * kSr));
    for (Frame i = 0; i < a.numFrames(); ++i)
        a.channels[0][(size_t) i] = level * (float) std::sin (2 * 3.14159265358979 * hz * (double) i / kSr) * (float) std::min (1.0, (double) i / 480.0);
    std::string err;
    if (! writeWav (path, a, WavFormat::Float32, err)) cstest::fail (__FILE__, __LINE__, err);
    return path;
}

struct Package
{
    std::vector<ZipInfo> entries;
    std::map<std::string, std::string> bytes;
    json manifest;
    bool has (const std::string& n) const { return bytes.count (n) > 0; }
    AudioData wav (const std::string& n, WavInfo* info = nullptr) const
    {
        AudioData a;
        std::string err;
        const std::string& b = bytes.at (n);
        if (! readWavFromMemory ((const uint8_t*) b.data(), b.size(), a, err, info)) cstest::fail (__FILE__, __LINE__, n + ": " + err);
        return a;
    }
};

Package openPackage (const std::string& path)
{
    Package p;
    std::string err;
    if (! listStoredZip (path, p.entries, err)) { cstest::fail (__FILE__, __LINE__, "zip: " + err); return p; }
    for (const auto& e : p.entries)
    {
        std::string b;
        if (! readStoredZipEntry (path, e, b)) cstest::fail (__FILE__, __LINE__, "no se lee " + e.name);
        p.bytes[e.name] = std::move (b);
    }
    if (p.has ("manifest.json")) p.manifest = json::parse (p.bytes["manifest.json"], nullptr, false);
    return p;
}

// Comprueba cada recurso del manifiesto contra los bytes reales del ZIP. Devuelve cuántos comprobó.
int verifyHashes (const Package& p)
{
    int n = 0;
    auto one = [&] (const json& r)
    {
        if (! r.is_object()) return;
        const std::string file = r.value ("file", std::string());
        CHECK_MSG (p.has (file), "falta en el zip: " + file);
        if (! p.has (file)) return;
        CHECK_MSG (sha256Hex (p.bytes.at (file)) == r.value ("sha256", std::string()), "hash distinto: " + file);
        CHECK ((long long) p.bytes.at (file).size() == r.value ("bytes", -1LL));
        ++n;
    };
    for (const auto& s : p.manifest["stems"]) one (s);
    one (p.manifest["mix"]); one (p.manifest["midi"]); one (p.manifest["project"]);
    return n;
}

// Canción de prueba: plantilla de reguetón (batería + bajo + acordes…), melodía de Solo AI, una pista de audio con
// un clip desplazado, una pista silenciada y un bus de reverb con dos envíos.
struct Song { std::string audioId, mutedId, busId; Frame clipAt = 0; };
Song buildSong (Fx& f)
{
    Song s;
    CHECK (f.session.createProject (f.dir + "/p", "Mi Canción: prueba/1", kSr, "reggaeton").ok);
    CHECK (f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 1 }, { "bars", 4 }, { "seed", 7 }, { "proposal", 0 } }).value ("ok", false));
    const auto a = f.session.addTrack ("Voz guía", "audio");
    const auto m = f.session.addTrack ("Descartada", "audio");
    const auto b = f.session.addTrack ("Reverb", "bus");
    CHECK (a.ok && m.ok && b.ok);
    s.audioId = a.id; s.mutedId = m.id; s.busId = b.id; s.clipAt = 30000;
    CHECK (f.session.importWav (toneWav (f.dir + "/tone.wav", 1.5, 330.0), a.id, s.clipAt).ok);
    CHECK (f.session.importWav (toneWav (f.dir + "/tone2.wav", 1.0, 990.0), m.id, 0).ok);
    CHECK (f.session.setTrackMute (m.id, true).ok);
    CHECK (f.session.setTrackVolumeDb (a.id, -4.0f).ok && f.session.setTrackPan (a.id, 0.4f).ok);
    const auto rev = f.session.addInsert (b.id, "cs.reverb");
    CHECK (rev.ok && f.session.setInsertParam (rev.id, "mix", 1.0f).ok);
    CHECK (f.session.setTrackSend (a.id, b.id, 0.6f).ok);
    CHECK (f.session.setTrackSend (f.session.model().tracks.at (0).id, b.id, 0.25f).ok);
    CHECK (f.session.setTrackSend (m.id, b.id, 0.5f).ok); // silenciada: no debe alimentar el bus (post-fader)
    CHECK (f.session.setMasterDb (-3.0f).ok);
    CHECK (f.session.save().ok);
    return s;
}

std::string projectFile (const Fx& f) { std::string s; readFile (f.dir + "/p/project.json", s); return s; }
} // namespace

TEST (daw_export_wet_package_is_complete_aligned_hashed_and_stems_sum_to_the_mix)
{
    Fx f ("daw_wet");
    const Song song = buildSong (f);
    const std::string before = projectFile (f);
    const json stateBefore = f.cmd ({ { "cmd", "state" } });
    const std::string out = f.dir + "/Mi_Cancion-cauce-daw.zip";

    // 32 bits flotante para poder comparar muestra a muestra (el valor por defecto de la app es 24 bits).
    const json started = f.cmd ({ { "cmd", "exportDaw" }, { "path", out }, { "mode", "wet" }, { "bits", 32 } });
    CHECK_MSG (started.value ("ok", false), started.dump());
    const json done = waitJob (f, started.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    const json res = done["result"];
    MEASURE ("paquete con efectos: %d archivos, %.1f MB, %d stems, %d notas MIDI, %.2f s de trabajo para %.1f s de canción",
             res.value ("files", 0), res.value ("bytes", 0LL) / 1048576.0, res.value ("stems", 0), res.value ("midiNotes", 0),
             res.value ("seconds", 0.0), res.value ("songSeconds", 0.0));
    CHECK (sha256File (out) == res.value ("sha256", std::string()));
    CHECK (! fileExists (out + ".parts") && ! fileExists (out + ".part"));

    const Package p = openPackage (out);
    CHECK (p.manifest.is_object());
    CHECK (p.has ("README.txt") && p.has ("manifest.json"));
    CHECK (p.manifest.value ("format", std::string()) == "cauce-daw" && p.manifest.value ("formatVersion", 0) == 1);
    CHECK (p.manifest.value ("mode", std::string()) == "wet");
    for (const auto& e : p.entries)
    {
        const bool known = e.name == "README.txt" || e.name == "manifest.json" || e.name.rfind ("stems/", 0) == 0 || e.name.rfind ("midi/", 0) == 0
                           || e.name.rfind ("mix/", 0) == 0 || e.name.rfind ("project/", 0) == 0;
        CHECK_MSG (known, "entrada inesperada: " + e.name);
        for (unsigned char c : e.name) CHECK_MSG (c < 0x80 && c != ' ' && c != '\\' && c != ':', "nombre no portable: " + e.name);
    }
    const int hashed = verifyHashes (p);
    CHECK (hashed == (int) p.entries.size() - 2); // todo menos manifest y README está en el manifiesto con su hash

    // Canción
    const json songJ = p.manifest["song"];
    const ProjectModel model = f.session.model();
    CHECK (songJ.value ("bpm", 0.0) == model.bpm && songJ.value ("sampleRate", 0) == kSr && songJ.value ("beatsPerBar", 0) == model.beatsPerBar);
    const Frame frames = songJ.value ("frames", (Frame) 0);
    CHECK (frames > model.contentEnd()); // incluye la cola de la reverb

    // Stems: mismo inicio, duración y frecuencia; una pista por archivo; el bus una sola vez.
    int returns = 0, tracks = 0;
    bool sawMuted = false, sawAudio = false, sawSolo = false, sawBus = false;
    std::vector<float> sumL ((size_t) frames, 0.0f), sumR ((size_t) frames, 0.0f);
    for (const auto& s : p.manifest["stems"])
    {
        WavInfo info;
        const AudioData a = p.wav (s.value ("file", std::string()), &info);
        CHECK (info.sampleRate == kSr && info.channels == 2 && info.frames == frames && info.isFloat);
        CHECK (s.value ("offsetFrames", -1) == 0 && s.value ("frames", (Frame) 0) == frames);
        const std::string role = s.value ("role", std::string());
        if (role == "busReturn")
        {
            float pk = 0;
            for (float v : a.channels[0]) pk = std::max (pk, std::fabs (v));
            CHECK (pk > 1e-3f); // el retorno lleva señal de verdad: la prueba de nulo no es trivial
        }
        if (role == "busReturn") { ++returns; sawBus = sawBus || s.value ("trackId", std::string()) == song.busId; CHECK (! s["fedBy"].empty()); }
        else { ++tracks; CHECK (role == "track"); }
        if (s.value ("trackId", std::string()) == song.mutedId) { sawMuted = true; CHECK (s.value ("muted", false) && ! s.value ("inMix", true)); }
        if (s.value ("name", std::string()).rfind ("Solo AI", 0) == 0) sawSolo = true;
        if (s.value ("trackId", std::string()) == song.audioId)
        {
            sawAudio = true;
            CHECK_MSG (s.value ("file", std::string()).find ("Voz_guia") != std::string::npos, s.value ("file", std::string()));
            // Clip desplazado: silencio exacto antes de su inicio, señal después.
            float pre = 0, post = 0;
            for (Frame i = 0; i < song.clipAt; ++i) pre = std::max (pre, std::fabs (a.channels[0][(size_t) i]));
            for (Frame i = song.clipAt; i < song.clipAt + kSr; ++i) post = std::max (post, std::fabs (a.channels[0][(size_t) i]));
            CHECK (pre == 0.0f && post > 0.05f);
        }
        if (s.value ("inMix", false))
            for (Frame i = 0; i < frames; ++i) { sumL[(size_t) i] += a.channels[0][(size_t) i]; sumR[(size_t) i] += a.channels[1][(size_t) i]; }
    }
    // Cada bus con envíos sale una sola vez (la plantilla trae el suyo, más el «Reverb» añadido aquí).
    int busesWithSends = 0;
    for (const auto& t : model.tracks)
    {
        if (t.kind != "bus") continue;
        bool fed = false;
        for (const auto& o : model.tracks) { auto it = o.sends.find (t.id); fed = fed || (o.kind != "bus" && it != o.sends.end() && it->second > 0); }
        busesWithSends += fed ? 1 : 0;
    }
    CHECK (returns == busesWithSends && sawBus && sawMuted && sawAudio && sawSolo);
    CHECK (tracks + returns == res.value ("stems", 0));

    // Prueba de nulo: suma de stems audibles × master = mezcla.
    WavInfo mi;
    const AudioData mix = p.wav (p.manifest["mix"].value ("file", std::string()), &mi);
    CHECK (mi.frames == frames && mi.sampleRate == kSr);
    const float master = dbToGain (-3.0f);
    double worst = 0, peak = 0;
    for (Frame i = 0; i < frames; ++i)
    {
        worst = std::max (worst, (double) std::fabs (sumL[(size_t) i] * master - mix.channels[0][(size_t) i]));
        worst = std::max (worst, (double) std::fabs (sumR[(size_t) i] * master - mix.channels[1][(size_t) i]));
        peak = std::max (peak, (double) std::fabs (mix.channels[0][(size_t) i]));
    }
    MEASURE ("prueba de nulo (suma de stems x master - mezcla): diferencia máxima %.2e con un pico de mezcla de %.3f (%.1f dB por debajo)",
             worst, peak, worst > 0 ? 20.0 * std::log10 (peak / worst) : 999.0);
    CHECK (peak > 0.05 && worst < 2e-5);

    // La mezcla del paquete es la misma que «Exportar mezcla».
    CHECK (f.session.exportMix (f.dir + "/mix.wav", 32).ok);
    AudioData ref;
    std::string err;
    CHECK (readWav (f.dir + "/mix.wav", ref, err));
    // El paquete recorta el silencio final (mismo punto para todos los archivos); lo recortado está por debajo de -100 dB.
    CHECK (ref.numFrames() >= frames);
    float cut = 0;
    for (Frame i = frames; i < ref.numFrames(); ++i) cut = std::max (cut, std::fabs (ref.channels[0][(size_t) i]));
    MEASURE ("duración: contenido %.2f s, paquete %.2f s, «Exportar mezcla» %.2f s; pico de lo recortado %.1e", (double) model.contentEnd() / kSr,
             (double) frames / kSr, (double) ref.numFrames() / kSr, cut);
    CHECK (cut < 1.0e-5f);
    double diffMix = 0;
    for (Frame i = 0; i < std::min (frames, ref.numFrames()); ++i) diffMix = std::max (diffMix, (double) std::fabs (ref.channels[0][(size_t) i] - mix.channels[0][(size_t) i]));
    CHECK (diffMix == 0.0);

    // El proyecto no cambia: ni el archivo, ni el modelo, ni el estado de deshacer.
    CHECK (projectFile (f) == before);
    const json stateAfter = f.cmd ({ { "cmd", "state" } });
    CHECK (stateAfter.value ("dirty", true) == stateBefore.value ("dirty", false) && stateAfter.value ("canUndo", false) == stateBefore.value ("canUndo", true));
    CHECK (toJson (f.session.model()) == toJson (model));

    // El .caucesound incluido se puede volver a importar y abrir.
    const std::string pj = p.manifest["project"].value ("file", std::string());
    CHECK (pj.size() > 11 && pj.compare (pj.size() - 11, 11, ".caucesound") == 0);
    {
        std::ofstream w (f.dir + "/back.caucesound", std::ios::binary);
        w.write (p.bytes.at (pj).data(), (std::streamsize) p.bytes.at (pj).size());
    }
    const ArchiveResult ar = importProjectArchive (f.dir + "/back.caucesound", f.dir + "/reimport");
    CHECK_MSG (ar.ok, ar.error);
    Fx g ("daw_wet_reopen");
    CHECK (g.session.openProject (f.dir + "/reimport").ok);
    CHECK (g.session.model().tracks.size() == model.tracks.size() && g.session.model().bpm == model.bpm);
    CHECK (g.session.exportMix (g.dir + "/mix2.wav", 32).ok);
    AudioData again;
    CHECK (readWav (g.dir + "/mix2.wav", again, err));
    double diffRe = 0;
    for (Frame i = 0; i < std::min (frames, again.numFrames()); ++i) diffRe = std::max (diffRe, (double) std::fabs (again.channels[0][(size_t) i] - mix.channels[0][(size_t) i]));
    MEASURE ("proyecto reimportado desde el paquete: su mezcla difiere %.1e de la original", diffRe);
    CHECK (again.numFrames() >= frames && diffRe == 0.0);
}

TEST (daw_export_dry_stems_are_clean_and_default_is_24_bit_pcm)
{
    Fx f ("daw_dry");
    const Song song = buildSong (f);
    const std::string out = f.dir + "/dry.zip";
    const json started = f.cmd ({ { "cmd", "exportDaw" }, { "path", out }, { "mode", "dry" } });
    CHECK_MSG (started.value ("ok", false), started.dump());
    const json done = waitJob (f, started.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    const Package p = openPackage (out);
    CHECK (p.manifest.value ("mode", std::string()) == "dry");
    verifyHashes (p);
    bool sawAudio = false;
    for (const auto& s : p.manifest["stems"])
    {
        CHECK (s.value ("role", std::string()) == "track"); // sin retornos de bus
        CHECK (! s.value ("volumeAndPanApplied", true));
        WavInfo info;
        const AudioData a = p.wav (s.value ("file", std::string()), &info);
        CHECK (info.bitsPerSample == 24 && ! info.isFloat && info.sampleRate == kSr);
        if (s.value ("trackId", std::string()) != song.audioId) continue;
        sawAudio = true;
        CHECK (s.value ("volumeDb", 0.0) == -4.0 && std::fabs (s.value ("pan", 0.0) - 0.4) < 1e-6);
        CHECK (s["sends"].size() == 1);
        // Limpia: el tono original, a su nivel (0,3), centrado, en su posición. 24 bits: error < 1e-6.
        AudioData src;
        std::string err;
        CHECK (readWav (f.dir + "/tone.wav", src, err));
        double worst = 0, lr = 0;
        for (Frame i = 0; i < src.numFrames(); ++i)
        {
            worst = std::max (worst, (double) std::fabs (a.channels[0][(size_t) (song.clipAt + i)] - src.channels[0][(size_t) i]));
            lr = std::max (lr, (double) std::fabs (a.channels[0][(size_t) (song.clipAt + i)] - a.channels[1][(size_t) (song.clipAt + i)]));
        }
        MEASURE ("stem sin efectos frente al audio original: diferencia máxima %.2e (24 bits), izquierda-derecha %.1e", worst, lr);
        CHECK (worst < 1e-6 && lr == 0.0);
    }
    CHECK (sawAudio);
    CHECK (done["result"].value ("clippedSamples", -1) == 0);
}

TEST (daw_export_rejects_bad_requests_and_cancel_or_close_leaves_nothing_behind)
{
    Fx f ("daw_cancel");
    CHECK (! f.cmd ({ { "cmd", "exportDaw" }, { "path", f.dir + "/x.zip" } }).value ("ok", true)); // sin proyecto
    CHECK (f.session.createProject (f.dir + "/empty", "vacío", kSr, "").ok);
    CHECK (! f.cmd ({ { "cmd", "exportDaw" }, { "path", f.dir + "/x.zip" } }).value ("ok", true)); // vacío
    f.session.close();
    buildSong (f);
    CHECK (! f.cmd ({ { "cmd", "exportDaw" }, { "path", "relativa.zip" } }).value ("ok", true));
    CHECK (! f.cmd ({ { "cmd", "exportDaw" }, { "path", f.dir + "/x.zip" }, { "bits", 8 } }).value ("ok", true));

    // Cancelar a mitad.
    const std::string out = f.dir + "/c.zip";
    const json a = f.cmd ({ { "cmd", "exportDaw" }, { "path", out } });
    CHECK (a.value ("ok", false));
    const json second = f.cmd ({ { "cmd", "exportDaw" }, { "path", f.dir + "/d.zip" } });
    CHECK_MSG (! second.value ("ok", true), "dos exportaciones a la vez"); // una sola exportación a la vez
    CHECK (f.cmd ({ { "cmd", "jobCancel" }, { "jobId", a.value ("jobId", std::string()) } }).value ("ok", false));
    const json done = waitJob (f, a.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "cancelled", done.dump());
    CHECK (! fileExists (out) && ! fileExists (out + ".part") && ! fileExists (out + ".parts"));

    // Cerrar el proyecto con la exportación en marcha: se cancela, espera y no deja nada.
    const json b = f.cmd ({ { "cmd", "exportDaw" }, { "path", out } });
    CHECK (b.value ("ok", false));
    for (int i = 0; i < 5000; ++i) // espera a que esté trabajando de verdad (no en cola)
    {
        const json st = f.cmd ({ { "cmd", "jobStatus" }, { "jobId", b.value ("jobId", std::string()) } });
        if (st.value ("state", std::string()) == "running" && fileExists (out + ".parts")) break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    CHECK (fileExists (out + ".parts"));
    const auto t0 = std::chrono::steady_clock::now();
    f.session.close();
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    MEASURE ("cerrar el proyecto con una exportación para DAW en marcha: %.0f ms", ms);
    CHECK (! fileExists (out) && ! fileExists (out + ".part") && ! fileExists (out + ".parts"));
    CHECK (ms < 250.0); // la cancelación se atiende dentro del render, no solo entre pistas

    // Después de todo eso, una exportación normal funciona y el proyecto se abre intacto.
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const json c = f.cmd ({ { "cmd", "exportDaw" }, { "path", out }, { "mix", false }, { "project", false } });
    const json ok = waitJob (f, c.value ("jobId", std::string()));
    CHECK_MSG (ok.value ("state", std::string()) == "done", ok.dump());
    const Package p = openPackage (out);
    CHECK (p.manifest["mix"].is_null() && p.manifest["project"].is_null());
    for (const auto& e : p.entries) CHECK (e.name.rfind ("mix/", 0) != 0 && e.name.rfind ("project/", 0) != 0);
}

TEST (smf_export_round_trips_notes_tempo_names_and_expands_repeated_patterns)
{
    Fx f ("smf");
    CHECK (f.session.createProject (f.dir + "/p", "midi", kSr, "trap").ok);
    CHECK (f.cmd ({ { "cmd", "setTempo" }, { "bpm", 143.5 } }).value ("ok", false));
    const auto syn = f.session.addTrack ("Melodía ñ", "synth");
    CHECK_MSG (syn.ok, syn.error);
    const auto clip = f.session.addMidiClip (syn.id, 3 * kPpq, 8 * kPpq);
    CHECK (clip.ok);
    std::vector<MidiNote> notes;
    for (int i = 0; i < 6; ++i) { MidiNote n; n.key = 60 + i * 2; n.velocity = 40 + i * 15; n.start = i * (kPpq / 2) + (i == 3 ? 37 : 0); n.length = kPpq / 4 + i * 10; notes.push_back (n); }
    CHECK (f.session.setClipNotes (clip.id, notes).ok);

    const ProjectModel m = f.session.model();
    const SmfExport smf = writeSmf (m);
    SmfParsed parsed;
    std::string err;
    CHECK_MSG (parseSmf (smf.bytes, parsed, err), err);
    CHECK (parsed.format == 1 && parsed.division == kPpq);
    CHECK_NEAR (parsed.bpm, 143.5, 0.001);
    CHECK (parsed.numerator == m.beatsPerBar && parsed.denominator == 4);
    CHECK (parsed.tracks.size() == smf.tracks.size() + 1);

    int total = 0, drumTracks = 0;
    for (const auto& info : smf.tracks)
    {
        const TrackModel* t = nullptr;
        for (const auto& tr : m.tracks) if (tr.id == info.trackId) t = &tr;
        CHECK (t != nullptr);
        if (! t) continue;
        const SmfParsedTrack& pt = parsed.tracks.at ((size_t) info.index);
        CHECK (pt.name == asciiText (t->name));
        for (unsigned char ch : pt.name) CHECK (ch < 0x80);
        // Esperado: los clips expandidos por el mismo código que usa el motor para sonar.
        std::vector<AbsNote> expect;
        for (const auto& c : t->midiClips)
        {
            const DrumPattern* pat = nullptr;
            for (const auto& dp : t->patterns) if (dp.id == c.patternId) pat = &dp;
            clipNotesAbsolute (c, pat, expect);
        }
        CHECK ((int) pt.notes.size() == info.notes && pt.notes.size() == expect.size());
        std::multiset<std::tuple<int64_t, int64_t, int, int>> a, b;
        for (const auto& n : pt.notes) { a.insert ({ n.tick, n.length, n.key, n.velocity }); CHECK (n.channel == info.channel); }
        for (const auto& n : expect) b.insert ({ n.start, n.end - n.start, n.key, n.velocity });
        CHECK (a == b);
        if (info.drums) { ++drumTracks; CHECK (info.channel == 9); for (const auto& n : pt.notes) CHECK (n.key >= kDrumBaseNote); }
        else CHECK (info.channel != 9);
        total += info.notes;
    }
    CHECK (total == smf.totalNotes && drumTracks >= 1);
    MEASURE ("MIDI: %zu pistas, %d notas, %zu bytes; tempo leído %.2f, compás %d/%d", smf.tracks.size(), total, smf.bytes.size(), parsed.bpm, parsed.numerator, parsed.denominator);

    CHECK (asciiText ("Melodía ñ · Ü—x") == "Melodia n - U-x");
    // La nota desplazada 37 ticks conserva su posición exacta (no se cuantiza).
    bool found = false;
    for (const auto& info : smf.tracks)
        if (info.trackId == syn.id)
            for (const auto& n : parsed.tracks.at ((size_t) info.index).notes) found = found || n.tick == 3 * kPpq + 3 * (kPpq / 2) + 37;
    CHECK (found);

    // Un patrón de batería de 1 compás en un clip de varios compases sale repetido.
    for (const auto& t : m.tracks)
    {
        if (t.kind != "drums" || t.midiClips.empty() || t.patterns.empty()) continue;
        std::vector<AbsNote> one, all;
        MidiClipModel c = t.midiClips[0];
        const DrumPattern* pat = nullptr;
        for (const auto& dp : t.patterns) if (dp.id == c.patternId) pat = &dp;
        if (! pat) continue;
        const Tick patTicks = (Tick) pat->steps * kStepTicks;
        c.start = 0; c.length = patTicks; clipNotesAbsolute (c, pat, one);
        c.length = patTicks * 3; clipNotesAbsolute (c, pat, all);
        CHECK (! one.empty() && all.size() == one.size() * 3);
        break;
    }

    // Comando exportMidi: archivo válido, sin tocar el proyecto.
    const std::string before = toJson (f.session.model());
    const json r = f.cmd ({ { "cmd", "exportMidi" }, { "path", f.dir + "/song.mid" } });
    CHECK_MSG (r.value ("ok", false) && r.value ("notes", 0) == total, r.dump());
    std::string bytes;
    CHECK (readFile (f.dir + "/song.mid", bytes));
    CHECK (std::vector<uint8_t> (bytes.begin(), bytes.end()) == smf.bytes);
    CHECK (toJson (f.session.model()) == before);
    CHECK (! f.cmd ({ { "cmd", "exportMidi" }, { "path", "relativa.mid" } }).value ("ok", true));
}

TEST (sha256_matches_known_vectors)
{
    CHECK (sha256Hex ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK (sha256Hex ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (sha256Hex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 h;
    const std::string chunk (1000, 'a');
    for (int i = 0; i < 1000; ++i) h.update (chunk.data(), chunk.size()); // un millón de «a»
    CHECK (h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
