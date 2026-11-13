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
    j["version"] = kProjectFormatVersionMax + 1; // 0.9: la v4 ya se lee; la futura es la 5
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

TEST (count_in_clicks_then_records_from_the_exact_start_position)
{
    Fixture f ("rec_countin");
    f.session.createProject (f.dir + "/p", "R"); // 120 BPM, 4/4: un compás = 96000 frames a 48 kHz
    const std::string t = f.session.addTrack ("Voz").id;
    f.session.setTrackArmed (t, true);
    f.engine.seek (96000);
    std::vector<float> o (64);
    f.engine.process (nullptr, 0, o.data(), 2, 0);
    CHECK (f.session.startRecording ("", 1, true, 1).ok);

    // Durante la cuenta: el transporte no avanza, no se graba y suenan 4 clics (el primero, acentuado).
    const int block = 1000; // no divide 96000: la cuenta termina a mitad de un bloque del anfitrión
    std::vector<float> in ((size_t) block), out ((size_t) block * 2), heard;
    Frame fed = 0;
    while (fed + block <= 96000)
    {
        for (int i = 0; i < block; ++i) in[(size_t) i] = (float) ((fed + i) % 4096) / 4096.0f;
        f.engine.process (in.data(), 1, out.data(), 2, block);
        for (int i = 0; i < block; ++i) heard.push_back (out[(size_t) i * 2]);
        fed += block;
        CHECK (f.engine.position() == 96000);
        CHECK (f.engine.countInRemaining() == 96000 - fed);
        CHECK (f.engine.recordStartPosition() < 0);
    }
    CHECK (fed == 96000);
    float peakAt[4] = { 0, 0, 0, 0 }, between = 0;
    for (size_t i = 0; i < heard.size(); ++i)
    {
        const size_t beat = i / 24000, off = i % 24000;
        if (off < 2400) peakAt[beat] = std::max (peakAt[beat], std::fabs (heard[i]));
        else if (off > 6000) between = std::max (between, std::fabs (heard[i]));
    }
    CHECK (peakAt[0] > 0.2f && peakAt[1] > 0.1f && peakAt[2] > 0.1f && peakAt[3] > 0.1f);
    CHECK (peakAt[0] > peakAt[1]);
    CHECK (between < 1e-4f);

    // Tras la cuenta: 1 s de toma. Empieza exactamente en la posición de partida y con la primera muestra
    // que entró después de la cuenta (la rampa de entrada sigue donde iba).
    Frame counter = fed;
    feedInput (f.engine, 48000, 192, counter);
    CHECK (f.engine.countInRemaining() == 0);
    auto r = f.session.stopRecording();
    CHECK_MSG (r.ok, r.error);
    auto m = f.session.model();
    const ClipModel* clip = nullptr;
    for (const auto& c : m.findTrack (t)->clips) clip = &c;
    CHECK (clip != nullptr);
    if (! clip) return;
    CHECK (clip->start == 96000);
    CHECK (clip->length == 48000);
    AudioData a;
    std::string err;
    CHECK (readWav (f.dir + "/p/" + m.findSource (clip->sourceId)->file, a, err));
    long wrong = 0;
    for (Frame i = 0; i < a.numFrames(); ++i) if (a.channels[0][(size_t) i] != (float) ((96000 + i) % 4096) / 4096.0f) ++wrong;
    CHECK (wrong == 0);
    MEASURE ("cuenta previa de 1 compás: clics %.2f/%.2f/%.2f/%.2f, toma colocada en %lld con %ld muestras erróneas", peakAt[0], peakAt[1], peakAt[2], peakAt[3], (long long) clip->start, wrong);

    // Parar durante la cuenta la cancela sin error, sin clip y sin dejar la cuenta pendiente para el siguiente play.
    const size_t clipsBefore = m.findTrack (t)->clips.size();
    f.engine.seek (0);
    CHECK (f.session.startRecording ("", 1, true, 2).ok);
    f.engine.process (in.data(), 1, out.data(), 2, block);
    CHECK (f.engine.countInRemaining() == 2 * 96000 - block);
    auto cancel = f.session.stopRecording();
    CHECK_MSG (cancel.ok, cancel.error);
    CHECK (cancel.id.empty());
    CHECK (f.session.model().findTrack (t)->clips.size() == clipsBefore);
    f.engine.process (in.data(), 1, out.data(), 2, block);
    CHECK (f.engine.countInRemaining() == 0);
    f.engine.play();
    f.engine.process (in.data(), 1, out.data(), 2, block);
    CHECK (f.engine.position() == block);
    f.engine.stop();
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

#include "cs/Api.h"

TEST (command_api_drives_a_full_session)
{
    Fixture f ("api");
    auto call = [&] (const nlohmann::json& q) { return nlohmann::json::parse (handleCommand (f.session, f.engine, q.dump())); };
    CHECK (call ({ { "cmd", "createProject" }, { "folder", f.dir + "/p" }, { "name", "API" } })["ok"] == true);
    const std::string t = call ({ { "cmd", "addTrack" }, { "name", "Voz" } })["id"];
    CHECK (call ({ { "cmd", "setVolume" }, { "trackId", t }, { "db", -3 } })["ok"] == true);
    auto imp = call ({ { "cmd", "importWav" }, { "path", writeTestWav (f.dir + "/in.wav", 48000, 1, 9600, 440) }, { "trackId", t }, { "at", 0 } });
    CHECK (imp["ok"] == true);
    CHECK (call ({ { "cmd", "splitClip" }, { "clipId", imp["id"] }, { "at", 4800 } })["ok"] == true);
    CHECK (call ({ { "cmd", "export" }, { "path", f.dir + "/o.wav" }, { "bits", 24 } })["ok"] == true);
    auto st = call ({ { "cmd", "state" } });
    CHECK (st["project"]["tracks"][0]["clips"].size() == 2);
    CHECK (st["canUndo"] == true);
    CHECK (call ({ { "cmd", "save" } })["ok"] == true);
    auto list = call ({ { "cmd", "listProjects" }, { "root", f.dir } });
    CHECK (list["projects"].size() == 1 && list["projects"][0]["name"] == "API");
    // Entradas no válidas: siempre respuesta JSON con error, nunca excepción ni cierre.
    CHECK (call ({ { "cmd", "nope" } })["ok"] == false);
    CHECK (call ({ { "cmd", "setVolume" }, { "trackId", 5 } })["ok"] == false);
    CHECK (nlohmann::json::parse (handleCommand (f.session, f.engine, "{{{"))["ok"] == false);
    CHECK (call ({ { "cmd", "moveClip" }, { "clipId", "x" }, { "start", "abc" } })["ok"] == false);
}

// ---------------------------------------------------------------------------------------------------------------
// Grabación limpia (0.8). Pregunta de Diego: ¿el beat que se oye en la toma es una mezcla digital del master o es
// el sonido de las bocinas que entra por el micrófono? Estas pruebas reproducen una plantilla fuerte con metrónomo y
// monitorización mientras se graba una entrada conocida, y comprueban que el archivo es EXACTAMENTE la entrada.
namespace
{
struct RecRun { std::vector<float> outL; };
// Igual que feedInput, pero guarda la salida izquierda y permite elegir la señal de entrada.
RecRun feedSignal (Engine& e, Frame frames, int block, Frame& counter, float (*sig) (Frame))
{
    RecRun r;
    std::vector<float> in ((size_t) block), out ((size_t) block * 2);
    Frame done = 0;
    while (done < frames)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        for (int i = 0; i < n; ++i) in[(size_t) i] = sig (counter + i);
        e.process (in.data(), 1, out.data(), 2, n);
        for (int i = 0; i < n; ++i) r.outL.push_back (out[(size_t) i * 2]);
        counter += n;
        done += n;
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }
    return r;
}
float voiceLike (Frame i) { return 0.3f * (float) std::sin (2.0 * M_PI * 220.0 * (double) i / 48000.0) * (float) (0.6 + 0.4 * std::sin (2.0 * M_PI * 3.0 * (double) i / 48000.0)); }
float silence (Frame) { return 0.0f; }
double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double a = 0;
    for (size_t i = from; i < to; ++i) a += (double) v[i] * v[i];
    return std::sqrt (a / (double) std::max<size_t> (1, to - from));
}
} // namespace

TEST (recording_takes_only_the_microphone_never_the_master_mix)
{
    Fixture f ("rec_clean");
    CHECK (f.session.createProject (f.dir + "/p", "R", 48000, "trap").ok);
    const std::string voz = f.session.addTrack ("Voz").id;
    CHECK (f.session.setTrackArmed (voz, true).ok);
    f.engine.setMetronome (true);
    f.engine.setMonitoring (true, 1.0f); // el peor caso: la voz también sale por la salida
    f.engine.seek (48000 * 2);
    std::vector<float> o (64);
    f.engine.process (nullptr, 0, o.data(), 2, 0);
    CHECK (f.session.startRecording ("", 1, true).ok);
    Frame counter = 0;
    const RecRun run = feedSignal (f.engine, 48000 * 3, 192, counter, voiceLike);
    auto r = f.session.stopRecording();
    CHECK_MSG (r.ok, r.error);
    // La salida tenía el beat (si no, la prueba no demostraría nada): bastante más energía que la voz sola.
    // Salida − voz monitorizada = lo que sonaba del proyecto (beat + metrónomo).
    std::vector<float> beat (run.outL.size());
    for (size_t i = 0; i < beat.size(); ++i) beat[i] = run.outL[i] - voiceLike ((Frame) i);
    const double outE = rms (beat, 0, beat.size());
    double voiceE = 0;
    for (Frame i = 0; i < 48000 * 3; ++i) voiceE += (double) voiceLike (i) * voiceLike (i);
    voiceE = std::sqrt (voiceE / (48000.0 * 3));
    CHECK (outE > 0.05);
    // La toma es la entrada, muestra a muestra: ni beat, ni metrónomo, ni monitorización.
    auto m = f.session.model();
    const ClipModel* clip = nullptr;
    for (const auto& c : m.findTrack (voz)->clips) clip = &c;
    CHECK (clip != nullptr);
    if (! clip) return;
    AudioData a;
    std::string err;
    CHECK (readWav (f.dir + "/p/" + m.findSource (clip->sourceId)->file, a, err));
    long wrong = 0;
    double maxErr = 0;
    for (Frame i = 0; i < a.numFrames(); ++i)
    {
        const double d = std::fabs ((double) a.channels[0][(size_t) i] - voiceLike (i));
        maxErr = std::max (maxErr, d);
        if (d > 1e-6) ++wrong;
    }
    CHECK (a.numFrames() == 48000 * 3);
    CHECK (wrong == 0);
    MEASURE ("toma de 3 s con plantilla trap + metrónomo + monitorización: beat en la salida %.3f RMS, voz %.3f RMS, error máximo de la toma %.1e (0 = exacta)", outE, voiceE, maxErr);
}

TEST (speaker_mode_ducks_only_the_live_output_while_recording)
{
    Fixture f ("rec_duck");
    CHECK (f.session.createProject (f.dir + "/p", "R", 48000, "trap").ok);
    const std::string voz = f.session.addTrack ("Voz").id;
    CHECK (f.session.setTrackArmed (voz, true).ok);
    const std::string before = toJson (f.session.model(), false);
    std::vector<float> o (64);
    // Referencia: los mismos 2 s reproducidos sin grabar.
    f.engine.seek (48000 * 4);
    f.engine.process (nullptr, 0, o.data(), 2, 0);
    f.engine.play();
    Frame c0 = 0;
    const RecRun ref = feedSignal (f.engine, 48000 * 2, 192, c0, silence);
    f.engine.stop();
    // Grabando con el modo bocinas a −12 dB.
    f.engine.setRecordDuck (-12.0f);
    f.engine.seek (48000 * 4);
    f.engine.process (nullptr, 0, o.data(), 2, 0);
    CHECK (f.session.startRecording ("", 1, true).ok);
    Frame c1 = 0;
    const RecRun ducked = feedSignal (f.engine, 48000 * 2, 192, c1, voiceLike);
    CHECK (f.session.stopRecording().ok);
    const size_t a0 = 4800, a1 = 96000; // tras la rampa inicial de ~20 ms
    const double ratio = rms (ducked.outL, a0, a1) / std::max (1e-9, rms (ref.outL, a0, a1));
    MEASURE ("modo bocinas −12 dB: la salida en vivo queda al %.3f de la referencia (esperado 0,251)", ratio);
    CHECK (ratio > 0.22 && ratio < 0.29);
    // Ni faders ni master cambiaron; solo se añadió la toma.
    auto m = f.session.model();
    ProjectModel b;
    std::string err;
    CHECK (fromJson (before, b, err));
    for (const auto& t : b.tracks) { const TrackModel* now = m.findTrack (t.id); CHECK (now && now->volumeDb == t.volumeDb && now->pan == t.pan); }
    CHECK (m.masterDb == b.masterDb);
    // Al parar la grabación la salida vuelve a su nivel normal (sin quedarse atenuada). La toma nueva se silencia para
    // comparar solo el beat.
    CHECK (f.session.setTrackMute (voz, true).ok);
    f.engine.seek (48000 * 4);
    f.engine.process (nullptr, 0, o.data(), 2, 0);
    f.engine.play();
    Frame c2 = 0;
    const RecRun after = feedSignal (f.engine, 48000 * 2, 192, c2, silence);
    f.engine.stop();
    const double back = rms (after.outL, a0, a1) / std::max (1e-9, rms (ref.outL, a0, a1));
    MEASURE ("tras grabar, la salida vuelve al %.3f de la referencia", back);
    CHECK (back > 0.9 && back < 1.15); // sin atenuar (atenuada sería ~0,25); el resto es el estado de colas de reverb y voces
    // La toma sigue siendo exacta con el modo bocinas.
    const ClipModel* clip = nullptr;
    for (const auto& c : m.findTrack (voz)->clips) clip = &c;
    CHECK (clip != nullptr);
    if (! clip) return;
    AudioData a;
    CHECK (readWav (f.dir + "/p/" + m.findSource (clip->sourceId)->file, a, err));
    long wrong = 0;
    for (Frame i = 0; i < a.numFrames(); ++i) if (std::fabs ((double) a.channels[0][(size_t) i] - voiceLike (i)) > 1e-6) ++wrong;
    CHECK (wrong == 0);
    f.engine.setRecordDuck (0.0f);
    CHECK (f.engine.recordDuck() == 1.0f);
}
