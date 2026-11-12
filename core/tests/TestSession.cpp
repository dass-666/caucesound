// Pruebas de la sesión: proyectos, edición, deshacer, grabación, recuperación y exportación.
#include "Test.h"

#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <random>
#include <thread>

using namespace cs;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::string writeTestWav (const std::string& path, int rate, int ch, Frame frames, double freq, float amp = 0.5f)
{
    AudioData a;
    a.sampleRate = rate;
    a.allocate (ch, frames);
    for (int c = 0; c < ch; ++c)
        for (Frame i = 0; i < frames; ++i) a.channels[(size_t) c][(size_t) i] = amp * (float) std::sin (2 * kPi * freq * (double) i / rate + c * 0.3);
    std::string err;
    writeWav (path, a, WavFormat::Float32, err);
    return path;
}

std::vector<float> renderLive (Engine& e, Frame from, Frame frames, int block = 256)
{
    e.seek (from);
    e.play();
    std::vector<float> out ((size_t) frames * 2);
    Frame done = 0;
    while (done < frames)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        e.process (nullptr, 0, out.data() + done * 2, 2, n);
        done += n;
    }
    e.stop();
    return out;
}

// Alimenta el motor como el callback de audio, con una entrada conocida (rampa por muestra).
void feedInput (Engine& e, Frame frames, int block, Frame& counter)
{
    std::vector<float> in ((size_t) block), out ((size_t) block * 2);
    Frame done = 0;
    while (done < frames)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) ((counter + i) % 4096) / 4096.0f;
        e.process (in.data(), 1, out.data(), 2, n);
        counter += n;
        done += n;
        std::this_thread::sleep_for (std::chrono::microseconds (200)); // da tiempo al hilo de disco
    }
}

struct Fixture
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fixture (const std::string& name)
    {
        dir = cstest::tempDir (name);
        engine.prepare (48000, 1024);
    }
};
} // namespace

TEST (project_create_edit_save_reopen_roundtrip)
{
    Fixture f ("proj_rt");
    const std::string p = f.dir + "/p1";
    CHECK (f.session.createProject (p, "Demo").ok);
    for (int i = 0; i < 4; ++i) CHECK (f.session.addTrack ("Pista " + std::to_string (i + 1)).ok);
    auto m = f.session.model();
    const std::string wav = writeTestWav (f.dir + "/in.wav", 48000, 2, 48000, 440);
    auto imp = f.session.importWav (wav, m.tracks[1].id, 4800);
    CHECK (imp.ok);
    CHECK (f.session.setTrackVolumeDb (m.tracks[1].id, -3.5f).ok);
    CHECK (f.session.setTrackPan (m.tracks[1].id, 0.25f).ok);
    CHECK (f.session.setTrackSolo (m.tracks[2].id, true).ok);
    CHECK (f.session.setTempo (98.5, 3).ok);
    CHECK (f.session.setClipFades (imp.id, 480, 960).ok);
    auto sp = f.session.splitClip (imp.id, 4800 + 24000);
    CHECK (sp.ok);
    CHECK (f.session.save().ok);
    const std::string before = toJson (f.session.model());
    f.session.close();

    LoadReport rep;
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (rep.missingSources.empty() && ! rep.usedBackup && rep.recoveredRecordings.empty());
    CHECK (toJson (f.session.model()) == before);
    // El audio importado reproduce lo mismo tras reabrir (comparación bit a bit del render)
    const auto a = renderLive (f.engine, 0, 60000);
    f.session.close();
    CHECK (f.session.openProject (p).ok);
    const auto b = renderLive (f.engine, 0, 60000);
    CHECK (a == b);
}

TEST (project_json_corruption_is_rejected_safely)
{
    Fixture f ("proj_fuzz");
    const std::string p = f.dir + "/p";
    f.session.createProject (p, "X");
    f.session.addTrack ("A");
    f.session.importWav (writeTestWav (f.dir + "/in.wav", 48000, 1, 4800, 220), "", 0);
    const std::string good = toJson (f.session.model(), false);
    std::mt19937 rng (11);
    int accepted = 0, rejected = 0;
    for (int it = 0; it < 3000; ++it)
    {
        std::string m = good;
        const int edits = 1 + (int) (rng() % 4);
        for (int e = 0; e < edits; ++e)
        {
            const size_t pos = rng() % m.size();
            switch (rng() % 3)
            {
                case 0: m[pos] = (char) (rng() % 128); break;
                case 1: m.erase (pos, 1 + rng() % 10); break;
                default: m.insert (pos, std::string (1, "0123456789-.,:{}[]\"e"[rng() % 20])); break;
            }
            if (m.empty()) m = "{";
        }
        ProjectModel out;
        std::string err;
        if (fromJson (m, out, err)) { ++accepted; CHECK (validate (out).empty()); }
        else ++rejected;
    }
    MEASURE ("3000 project.json mutados: %d aceptados (siguen siendo válidos), %d rechazados con error", accepted, rejected);
    // versión futura
    auto j = nlohmann::json::parse (good);
    j["version"] = kProjectFormatVersion + 1;
    ProjectModel out;
    std::string err;
    CHECK (! fromJson (j.dump(), out, err));
}

TEST (project_survives_crash_during_save)
{
    Fixture f ("proj_crash");
    const std::string p = f.dir + "/p";
    CHECK (f.session.createProject (p, "Seguro").ok);
    f.session.addTrack ("A");
    CHECK (f.session.save().ok);
    f.session.addTrack ("B");
    CHECK (f.session.save().ok); // ahora existe project.json (2 pistas) y project.json.bak (1 pista)
    f.session.close();
    namespace fs = std::filesystem;

    // Caso 1: el cierre ocurrió entre "mover a .bak" y "renombrar .tmp": no hay project.json, sí .tmp completo.
    fs::copy_file (p + "/project.json", p + "/project.json.tmp", fs::copy_options::overwrite_existing);
    fs::remove (p + "/project.json");
    LoadReport rep;
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (rep.usedBackup);
    CHECK (f.session.model().tracks.size() == 2);
    f.session.close();

    // Caso 2: project.json quedó truncado (escritura a medias en un sistema sin rename atómico): se usa .bak.
    {
        std::string text;
        readFile (p + "/project.json", text);
        std::string err;
        atomicWriteFile (p + "/project.json", text.substr (0, text.size() / 2), err, false);
        fs::remove (p + "/project.json.tmp");
    }
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (rep.usedBackup);
    CHECK (! f.session.model().tracks.empty());
    f.session.close();

    // Caso 3: todo dañado: error claro, nada se sobrescribe.
    std::string err;
    atomicWriteFile (p + "/project.json", "basura", err, false);
    atomicWriteFile (p + "/project.json.bak", "{}", err, false);
    fs::remove (p + "/project.json.tmp");
    auto r = f.session.openProject (p);
    CHECK (! r.ok);
    CHECK (! r.error.empty());
    std::string still;
    readFile (p + "/project.json", still);
    CHECK (still == "basura");
}

TEST (clip_editing_and_undo_redo)
{
    Fixture f ("edit");
    f.session.createProject (f.dir + "/p", "E");
    const std::string t1 = f.session.addTrack ("A").id, t2 = f.session.addTrack ("B").id;
    const std::string c = f.session.importWav (writeTestWav (f.dir + "/in.wav", 48000, 1, 48000, 300), t1, 1000).id;
    const auto original = renderLive (f.engine, 0, 50000);

    // Dividir no cambia el sonido (sin fundidos): bit a bit idéntico
    auto right = f.session.splitClip (c, 1000 + 12345);
    CHECK (right.ok);
    CHECK (renderLive (f.engine, 0, 50000) == original);

    // Mover a otra pista y volver: idéntico
    CHECK (f.session.moveClip (right.id, 1000 + 12345, t2).ok);
    CHECK (renderLive (f.engine, 0, 50000) == original);

    // Recorte por la izquierda: el contenido no se desplaza en el timeline
    CHECK (f.session.trimClip (c, 3000, 1000 + 12345 - 3000).ok);
    auto trimmed = renderLive (f.engine, 0, 50000);
    for (Frame i = 0; i < 3000; ++i) CHECK (trimmed[(size_t) i * 2] == 0.0f);
    for (Frame i = 3000; i < 50000; i += 97) CHECK (trimmed[(size_t) i * 2] == original[(size_t) i * 2]);
    CHECK (! f.session.trimClip (c, 0, 99999999).ok); // fuera de los límites
    CHECK (! f.session.splitClip (c, 10).ok);

    // Mezcla fuera del deshacer: el volumen se conserva al deshacer ediciones
    CHECK (f.session.setTrackVolumeDb (t1, -6.0f).ok);
    CHECK (f.session.undo().ok); // deshace el recorte
    CHECK (f.session.model().findTrack (t1)->volumeDb == -6.0f);
    CHECK (f.session.setTrackVolumeDb (t1, 0.0f).ok);
    CHECK (f.session.undo().ok); // deshace el movimiento
    CHECK (f.session.undo().ok); // deshace la división
    CHECK (f.session.model().findTrack (t1)->clips.size() == 1);
    CHECK (renderLive (f.engine, 0, 50000) == original);
    CHECK (f.session.redo().ok);
    CHECK (f.session.model().findTrack (t1)->clips.size() == 2);

    // Borrar y duplicar
    auto dup = f.session.duplicateClip (c);
    CHECK (dup.ok);
    CHECK (f.session.deleteClip (dup.id).ok);
    CHECK (! f.session.deleteClip ("clp_inexistente").ok);
    CHECK (f.session.canUndo());
}

TEST (recording_places_audio_at_the_right_position)
{
    for (Frame offset : { (Frame) 0, (Frame) 480 })
    {
        Fixture f ("rec");
        f.session.createProject (f.dir + "/p", "R");
        const std::string t = f.session.addTrack ("Voz").id;
        f.session.setTrackArmed (t, true);
        f.session.setRecordLatencyOffset (offset);
        f.engine.seek (96000); // empezamos a grabar en el segundo 2
        Frame counter = 0;
        std::vector<float> o (64);
        f.engine.process (nullptr, 0, o.data(), 2, 0);
        CHECK (f.session.startRecording ("", 1, true).ok);
        feedInput (f.engine, 48000 * 3, 192, counter); // 3 s
        auto r = f.session.stopRecording();
        CHECK_MSG (r.ok, r.error);
        auto m = f.session.model();
        const ClipModel* clip = nullptr;
        for (const auto& c : m.findTrack (t)->clips) clip = &c;
        CHECK (clip != nullptr);
        if (! clip) return;
        CHECK (clip->start == 96000 - offset);
        CHECK (clip->length == 48000 * 3);
        // El contenido es exactamente la entrada (rampa): comprobamos el archivo en disco.
        AudioData a;
        std::string err;
        CHECK (readWav (f.dir + "/p/" + m.findSource (clip->sourceId)->file, a, err));
        long wrong = 0;
        for (Frame i = 0; i < a.numFrames(); ++i) if (a.channels[0][(size_t) i] != (float) (i % 4096) / 4096.0f) ++wrong;
        CHECK (wrong == 0);
        CHECK (! fileExists (f.dir + "/p/audio/" + clip->sourceId + ".rec.json"));
        // Guardado automático tras grabar: el proyecto en disco ya contiene el clip.
        ProjectModel onDisk;
        std::string text;
        readFile (f.dir + "/p/project.json", text);
        CHECK (fromJson (text, onDisk, err));
        CHECK (onDisk.findSource (clip->sourceId) != nullptr);
        if (offset == 480) MEASURE ("grabación de 3 s: %lld frames exactos, colocada en %lld (inicio %d - compensación %lld)", (long long) clip->length, (long long) clip->start, 96000, (long long) offset);
    }
}

TEST (recording_is_recovered_after_unexpected_close)
{
    Fixture f ("rec_crash");
    const std::string p = f.dir + "/p";
    f.session.createProject (p, "Crash");
    const std::string t = f.session.addTrack ("Voz").id;
    f.session.setTrackArmed (t, true);
    f.session.save();
    f.engine.seek (4800);
    Frame counter = 0;
    CHECK (f.session.startRecording (t, 1, true).ok);
    feedInput (f.engine, 48000 * 2, 256, counter);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    f.session.abandonForTest(); // "la app se cerró": sin stopRecording ni guardado

    // Encabezado sin actualizar, como tras un cierre a mitad de escritura.
    std::string recId;
    for (const auto& n : listDir (p + "/audio")) if (n.find (".wav.part") != std::string::npos) recId = n.substr (0, n.find (".wav.part"));
    CHECK (! recId.empty());
    {
        std::FILE* fp = std::fopen ((p + "/audio/" + recId + ".wav.part").c_str(), "r+b");
        uint8_t zero[4] = { 0, 0, 0, 0 };
        std::fseek (fp, 40, SEEK_SET);
        std::fwrite (zero, 1, 4, fp);
        std::fclose (fp);
    }

    Engine e2;
    e2.prepare (48000, 1024);
    Session s2 (e2);
    LoadReport rep;
    CHECK (s2.openProject (p, &rep).ok);
    CHECK (rep.recoveredRecordings.size() == 1);
    auto m = s2.model();
    CHECK (m.findTrack (t) && m.findTrack (t)->clips.size() == 1);
    if (m.findTrack (t) && ! m.findTrack (t)->clips.empty())
    {
        const auto& c = m.findTrack (t)->clips[0];
        CHECK (c.start == 4800);
        MEASURE ("grabación interrumpida: %lld de %lld frames recuperados", (long long) c.length, (long long) counter);
        CHECK (c.length == counter); // el hilo de disco había vaciado toda la cola antes del cierre
    }
    // Abrir de nuevo no duplica la recuperación
    s2.close();
    LoadReport rep2;
    CHECK (s2.openProject (p, &rep2).ok);
    CHECK (rep2.recoveredRecordings.empty());
    CHECK (s2.model().findTrack (t)->clips.size() == 1);
}

TEST (import_resamples_to_project_rate)
{
    Fixture f ("import");
    f.session.createProject (f.dir + "/p", "I");
    const std::string t = f.session.addTrack ("A").id;
    auto r = f.session.importWav (writeTestWav (f.dir + "/in44.wav", 44100, 2, 44100, 1000), t, 0);
    CHECK (r.ok);
    auto m = f.session.model();
    CHECK (std::llabs (m.sources[0].frames - 48000) <= 1);
    CHECK (m.sources[0].channels == 2);
    CHECK (! f.session.importWav (f.dir + "/no_existe.wav", t, 0).ok);
    std::string err;
    atomicWriteFile (f.dir + "/fake.wav", "RIFF1234WAVEjunk", err, false);
    CHECK (! f.session.importWav (f.dir + "/fake.wav", t, 0).ok);
    CHECK (f.session.model().sources.size() == 1); // un fallo no deja restos en el modelo
}

TEST (export_matches_live_playback_and_formats_are_valid)
{
    Fixture f ("export");
    f.session.createProject (f.dir + "/p", "Mix");
    std::vector<std::string> tracks;
    for (int i = 0; i < 4; ++i) tracks.push_back (f.session.addTrack ("T" + std::to_string (i)).id);
    for (int i = 0; i < 4; ++i)
    {
        auto c = f.session.importWav (writeTestWav (f.dir + "/s" + std::to_string (i) + ".wav", 48000, 1 + i % 2, 30000 + 5000 * i, 200.0 + 110 * i, 0.2f), tracks[(size_t) i], 1000 * i);
        f.session.setClipFades (c.id, 100, 2000);
        f.session.setTrackPan (tracks[(size_t) i], -0.6f + 0.4f * i);
        f.session.setTrackVolumeDb (tracks[(size_t) i], -2.0f * i);
    }
    f.session.setTrackMute (tracks[3], true);
    const Frame end = f.session.model().contentEnd();
    const auto live = renderLive (f.engine, 0, end, 333);

    const std::string out32 = f.dir + "/mix32.wav";
    CHECK (f.session.exportMix (out32, 32).ok);
    AudioData a;
    std::string err;
    CHECK (readWav (out32, a, err));
    CHECK (a.numFrames() == end && a.numChannels() == 2);
    long diff = 0;
    for (Frame i = 0; i < end; ++i)
        if (a.channels[0][(size_t) i] != live[(size_t) i * 2] || a.channels[1][(size_t) i] != live[(size_t) i * 2 + 1]) ++diff;
    MEASURE ("exportación float 32 frente a reproducción en vivo: %ld muestras distintas de %lld", diff, (long long) end * 2);
    CHECK (diff == 0);

    for (int bits : { 16, 24 })
    {
        const std::string o = f.dir + "/mix" + std::to_string (bits) + ".wav";
        long clipped = -1;
        CHECK (f.session.exportMix (o, bits, 4800, &clipped).ok);
        WavInfo info;
        CHECK (readWav (o, a, err, &info));
        CHECK (info.bitsPerSample == bits && ! info.headerRepaired && a.numFrames() == end + 4800);
        CHECK (clipped == 0);
        double maxErr = 0;
        for (Frame i = 0; i < end; ++i) maxErr = std::max (maxErr, (double) std::fabs (a.channels[0][(size_t) i] - live[(size_t) i * 2]));
        MEASURE ("exportación %d bits: error máximo frente al render float %.2e", bits, maxErr);
        CHECK (maxErr < (bits == 16 ? 3.0 / 32768 : 2.0 / 8388607));
    }
    // Determinismo (incluido el dither de 16 bits): dos exportaciones idénticas byte a byte.
    f.session.exportMix (f.dir + "/a.wav", 16);
    f.session.exportMix (f.dir + "/b.wav", 16);
    std::string x, y;
    readFile (f.dir + "/a.wav", x);
    readFile (f.dir + "/b.wav", y);
    CHECK (! x.empty() && x == y);
}

TEST (missing_audio_is_reported_and_plays_silence)
{
    Fixture f ("missing");
    const std::string p = f.dir + "/p";
    f.session.createProject (p, "M");
    const std::string t = f.session.addTrack ("A").id;
    auto c = f.session.importWav (writeTestWav (f.dir + "/in.wav", 48000, 1, 4800, 440), t, 0);
    f.session.close();
    for (const auto& n : listDir (p + "/audio")) removeFile (p + "/audio/" + n);
    LoadReport rep;
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (rep.missingSources.size() == 1);
    const auto out = renderLive (f.engine, 0, 4800);
    for (float v : out) CHECK (v == 0.0f);
    auto st = nlohmann::json::parse (f.session.stateJson());
    CHECK (st["missingClips"].size() == 1 && st["missingClips"][0] == c.id);
}

TEST (waveform_peaks_are_correct)
{
    Fixture f ("peaks");
    f.session.createProject (f.dir + "/p", "P");
    const std::string t = f.session.addTrack ("A").id;
    AudioData a;
    a.sampleRate = 48000;
    a.allocate (1, 48000);
    a.channels[0][1000] = 0.9f;
    a.channels[0][30000] = -0.7f;
    std::string err;
    writeWav (f.dir + "/sp.wav", a, WavFormat::Float32, err);
    f.session.importWav (f.dir + "/sp.wav", t, 0);
    const std::string sid = f.session.model().sources[0].id;
    for (int buckets : { 10, 100, 4000 })
    {
        auto pk = f.session.peaks (sid, 0, 48000, buckets);
        CHECK (pk.size() == (size_t) buckets * 2);
        float mx = 0, mn = 0;
        int bmax = -1, bmin = -1;
        for (int b = 0; b < buckets; ++b)
        {
            if (pk[(size_t) b * 2 + 1] > mx) { mx = pk[(size_t) b * 2 + 1]; bmax = b; }
            if (pk[(size_t) b * 2] < mn) { mn = pk[(size_t) b * 2]; bmin = b; }
        }
        CHECK_NEAR (mx, 0.9, 1e-6);
        CHECK_NEAR (mn, -0.7, 1e-6);
        CHECK (bmax == (int) (1000.0 * buckets / 48000));
        CHECK (bmin == (int) (30000.0 * buckets / 48000));
    }
}
