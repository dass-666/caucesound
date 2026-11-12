// Exportación para DAW (misión 0.6): un paquete ZIP con archivos estándar para seguir trabajando la canción en
// FL Studio, Reaper, Cubase u otro programa. No es un formato nativo de ningún DAW: son WAV + MIDI + un manifiesto.
//
//   stems/     una pista por archivo WAV, todas con el mismo inicio (0), la misma duración y la misma frecuencia
//   midi/      un archivo MIDI estándar (formato 1) con todas las pistas de instrumento
//   mix/       la mezcla completa, igual que «Exportar mezcla»
//   project/   el proyecto .caucesound original (para volver a CAUCESOUND sin pérdidas)
//   manifest.json, README.txt
//
// Modos (cs::DawExportOptions::wet):
//   Con efectos (wet): cada pista sale con sus inserciones, su volumen y su panorama, SIN sus envíos a buses. Cada
//     bus con envíos sale como un stem aparte («retorno»): el efecto compartido se exporta una sola vez, no repartido
//     ni duplicado en las pistas. Sin ganancia de master. Así, la suma de todos los stems que suenan en la mezcla es
//     la mezcla (antes del master), y la prueba lo comprueba muestra a muestra.
//   Sin efectos (dry): cada pista sale sin inserciones, a 0 dB y centrada, para mezclar desde cero. No hay retornos
//     de bus. El manifiesto indica el volumen, el panorama, los efectos y los envíos que NO se aplicaron.
// Las pistas silenciadas también se exportan (sin silenciar) y se marcan en el manifiesto.
//
// La exportación no modifica el proyecto: se toma una copia del modelo y grafos de render propios con el cerrojo de
// la sesión, y todo el trabajo pesado ocurre después, sin el cerrojo y fuera del hilo de audio, en el JobRunner.
#include "cs/Archive.h"
#include "cs/Hash.h"
#include "cs/Instruments.h"
#include "cs/Session.h"
#include "cs/Smf.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace cs
{
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
struct ParamSnap { float gain = 1, pan = 0; bool mute = false, solo = false; float send[kMaxBuses] = {}; };

struct StemPlan
{
    std::string role;    // "track" | "busReturn" | "mix"
    std::string trackId, name, file;
    std::unique_ptr<RenderGraph> graph;
    std::vector<ParamSnap> params; // por slot
    float master = 1.0f;
    json info;           // datos del manifiesto que solo se conocen con la sesión delante
};

// Nombre de archivo portable: ASCII, sin espacios ni separadores. «Voz 1 (toma)» -> «Voz_1_toma».
// (asciiText convierte antes «Melodía» en «Melodia», para no perder letras.)
std::string safeName (const std::string& name, const char* fallback)
{
    std::string out;
    bool lastUnderscore = true;
    for (unsigned char c : asciiText (name))
    {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (keep) { out.push_back ((char) c); lastUnderscore = false; }
        else if (! lastUnderscore && c < 0x80) { out.push_back ('_'); lastUnderscore = true; }
        if (out.size() >= 40) break;
    }
    while (! out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string (fallback) : out;
}

std::string two (int n) { return (n < 10 ? "0" : "") + std::to_string (n); }

std::string isoNow()
{
    const std::time_t t = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s (&tm, &t);
#else
    gmtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

struct ScopedDir
{
    std::string path;
    ~ScopedDir() { std::error_code ec; if (! path.empty()) fs::remove_all (path, ec); }
};

constexpr float kSilence = 1.0e-5f; // -100 dB

// Acorta un WAV canónico (cabecera de 44 bytes, como los que escribe writeWav) a 'frames' frames.
bool truncateWavFile (const std::string& path, Frame frames, int bytesPerFrame)
{
    const uint32_t dataBytes = (uint32_t) ((uint64_t) frames * (uint64_t) bytesPerFrame);
    std::error_code ec;
    if (fs::file_size (path, ec) < 44ull + dataBytes || ec) return false;
    fs::resize_file (path, 44ull + dataBytes, ec);
    if (ec) return false;
    std::fstream f (path, std::ios::in | std::ios::out | std::ios::binary);
    auto put = [&] (std::streamoff at, uint32_t v)
    {
        const char b[4] = { (char) (v & 0xFF), (char) ((v >> 8) & 0xFF), (char) ((v >> 16) & 0xFF), (char) ((v >> 24) & 0xFF) };
        f.seekp (at); f.write (b, 4);
    };
    char tag[4] = {};
    f.seekg (36); f.read (tag, 4);
    if (std::string (tag, 4) != "data") return false; // no es la cabecera que conocemos: mejor fallar que estropearlo
    put (4, 36u + dataBytes);
    put (40, dataBytes);
    return (bool) f;
}

bool writeText (const std::string& path, const std::string& text)
{
    std::ofstream f (path, std::ios::binary);
    f.write (text.data(), (std::streamsize) text.size());
    return (bool) f;
}
} // namespace

OpResult Session::exportDaw (const std::string& outPath, const DawExportOptions& opt)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (recorder) return OpResult::fail ("no se puede exportar durante la grabación");
    if (outPath.empty() || ! fs::path (outPath).is_absolute()) return OpResult::fail ("la ruta de exportación debe ser absoluta");
    if (proj.contentEnd() <= 0) return OpResult::fail ("el proyecto está vacío");
    if (opt.bitDepth != 16 && opt.bitDepth != 24 && opt.bitDepth != 32) return OpResult::fail ("profundidad de bits no admitida (16, 24 o 32)");
    for (const auto& j : jobs.list())
        if (j.type == "dawExport" && (j.state == "queued" || j.state == "running")) return OpResult::fail ("ya hay una exportación en curso");

    // Duración común: contenido + la cola más larga (instrumentos, inserciones y buses), igual que la mezcla.
    Frame instrumentTail = 0, busTail = 0;
    {
        RenderGraph scratch;
        int slot = 0;
        for (const auto& t : proj.tracks)
        {
            TrackRT tr = makeTrackRT (t, slot++, true, scratch);
            Frame tail = tr.instrument && ! tr.events.empty() ? tr.instrument->tailSamples() : 0;
            for (const auto& fx : tr.inserts) tail += fx->tailSamples();
            if (tr.busIndex >= 0) busTail = std::max (busTail, tail); else instrumentTail = std::max (instrumentTail, tail);
        }
    }
    const Frame total = proj.contentEnd() + instrumentTail + busTail;
    const double seconds = (double) total / proj.sampleRate;
    if (seconds > kDawExportMaxSeconds) return OpResult::fail ("la canción es demasiado larga para exportarla por pistas (máximo 20 minutos)");

    bool anySolo = false;
    for (const auto& t : proj.tracks) anySolo = anySolo || (t.solo && t.kind != "bus");
    auto snapOf = [&] (const TrackModel& t)
    {
        ParamSnap p;
        p.gain = dbToGain (t.volumeDb); p.pan = t.pan; p.mute = t.mute; p.solo = t.solo;
        for (const auto& [busId, level] : t.sends)
        {
            const int b = busIndexOf (busId);
            if (b >= 0 && b < kMaxBuses) p.send[b] = std::clamp (level, 0.0f, 1.0f);
        }
        return p;
    };
    auto newGraph = [&]
    {
        auto g = std::make_unique<RenderGraph>();
        g->sampleRate = proj.sampleRate; g->bpm = proj.bpm; g->beatsPerBar = proj.beatsPerBar; g->contentEnd = proj.contentEnd();
        return g;
    };
    auto insertTypes = [] (const TrackModel& t, bool active)
    {
        json a = json::array();
        for (const auto& i : t.inserts) if (i.bypass != active) a.push_back (i.type);
        return a;
    };

    auto plans = std::make_shared<std::vector<StemPlan>>();
    int number = 0;
    for (const auto& t : proj.tracks)
    {
        if (t.kind == "bus") continue;
        if (t.clips.empty() && t.midiClips.empty()) continue; // pista vacía: no genera archivo (se anota en el manifiesto)
        StemPlan sp;
        sp.role = "track"; sp.trackId = t.id; sp.name = t.name;
        sp.file = "stems/" + two (++number) + "_" + safeName (t.name, "pista") + ".wav";
        sp.graph = newGraph();
        TrackRT tr = makeTrackRT (t, 0, true, *sp.graph);
        ParamSnap p;
        if (opt.wet) { p.gain = dbToGain (t.volumeDb); p.pan = t.pan; }
        else tr.inserts.clear();
        sp.graph->tracks.push_back (std::move (tr));
        sp.params.push_back (p);
        const bool audible = ! t.mute && (! anySolo || t.solo);
        json sends = json::array();
        for (const auto& [busId, level] : t.sends) if (level > 0 && busIndexOf (busId) >= 0) sends.push_back ({ { "busId", busId }, { "level", level } });
        sp.info = { { "kind", t.kind }, { "muted", t.mute }, { "solo", t.solo }, { "inMix", audible }, { "volumeDb", t.volumeDb }, { "pan", t.pan },
                    { "volumeAndPanApplied", opt.wet }, { "insertsApplied", opt.wet ? insertTypes (t, true) : json::array() },
                    { "insertsNotApplied", opt.wet ? json::array() : insertTypes (t, true) }, { "sends", sends },
                    { "clips", (int) (t.clips.size() + t.midiClips.size()) } };
        plans->push_back (std::move (sp));
    }
    json skipped = json::array();
    for (const auto& t : proj.tracks)
        if (t.kind != "bus" && t.clips.empty() && t.midiClips.empty()) skipped.push_back ({ { "trackId", t.id }, { "name", t.name }, { "reason", "pista vacía" } });

    if (opt.wet)
    {
        for (const auto& bus : proj.tracks)
        {
            if (bus.kind != "bus") continue;
            const int b = busIndexOf (bus.id);
            if (b < 0 || b >= kMaxBuses) continue;
            StemPlan sp;
            sp.role = "busReturn"; sp.trackId = bus.id; sp.name = bus.name;
            sp.graph = newGraph();
            json feeders = json::array();
            int slot = 0;
            for (const auto& t : proj.tracks)
            {
                if (t.kind == "bus") continue;
                auto it = t.sends.find (bus.id);
                if (it == t.sends.end() || it->second <= 0) continue;
                TrackRT tr = makeTrackRT (t, slot++, true, *sp.graph);
                tr.sendsOnly = true; // solo alimenta el bus: su señal directa ya está en su propio stem
                sp.graph->tracks.push_back (std::move (tr));
                ParamSnap p = snapOf (t);
                for (int k = 0; k < kMaxBuses; ++k) if (k != b) p.send[k] = 0;
                sp.params.push_back (p);
                feeders.push_back (t.id);
            }
            if (feeders.empty()) { skipped.push_back ({ { "trackId", bus.id }, { "name", bus.name }, { "reason", "bus sin envíos" } }); continue; }
            TrackRT br = makeTrackRT (bus, slot, true, *sp.graph);
            sp.graph->tracks.push_back (std::move (br));
            ParamSnap bp;
            bp.gain = dbToGain (bus.volumeDb); bp.pan = bus.pan; bp.mute = bus.mute;
            sp.params.push_back (bp);
            sp.file = "stems/" + two (++number) + "_retorno_" + safeName (bus.name, "bus") + ".wav";
            sp.info = { { "kind", "bus" }, { "muted", bus.mute }, { "solo", false }, { "inMix", ! bus.mute }, { "volumeDb", bus.volumeDb }, { "pan", bus.pan },
                        { "volumeAndPanApplied", true }, { "insertsApplied", insertTypes (bus, true) }, { "insertsNotApplied", json::array() },
                        { "fedBy", feeders },
                        { "note", "Retorno del efecto compartido, exportado una sola vez. Respeta los silencios y solos de las pistas que lo alimentan." } };
            plans->push_back (std::move (sp));
        }
    }
    if (plans->empty()) return OpResult::fail ("no hay ninguna pista con contenido que exportar");

    if (opt.includeMix)
    {
        StemPlan sp;
        sp.role = "mix"; sp.name = proj.name;
        sp.file = "mix/" + safeName (proj.name, "cancion") + "_mezcla.wav";
        sp.graph = newGraph();
        int slot = 0;
        for (const auto& t : proj.tracks)
        {
            sp.graph->tracks.push_back (makeTrackRT (t, slot++, true, *sp.graph));
            sp.params.push_back (snapOf (t));
        }
        sp.master = dbToGain (proj.masterDb);
        plans->push_back (std::move (sp));
    }

    // Copia del modelo: el trabajo no vuelve a mirar la sesión.
    auto snapshot = std::make_shared<const ProjectModel> (proj);
    const std::string projectDir = dir, projectId = proj.id;
    const SmfExport smf = writeSmf (proj);
    json lanes = json::object();
    for (const auto& t : proj.tracks)
    {
        if (t.kind != "drums") continue;
        const int kit = drumKitIndex (t.instrument.preset);
        if (kit >= 0) lanes[t.id] = drumKits()[(size_t) kit].laneNames;
    }
    const DawExportOptions options = opt;

    const std::string jobId = jobs.submit ("dawExport", projectId, outPath,
        [plans, snapshot, projectDir, smf, lanes, skipped, options, outPath, total, anySolo] (JobContext& ctx, std::string& result, std::string& error)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const ProjectModel& p = *snapshot;
            ScopedDir stage;
            stage.path = outPath + ".parts";
            std::error_code ec;
            fs::remove_all (stage.path, ec);
            fs::create_directories (stage.path + "/stems", ec);
            fs::create_directories (stage.path + "/midi", ec);
            fs::create_directories (stage.path + "/mix", ec);
            fs::create_directories (stage.path + "/project", ec);
            if (ec) { error = "no se pudo crear la carpeta temporal de exportación"; return false; }

            std::vector<ZipSource> entries;
            auto resource = [&] (const std::string& rel, json& j) -> bool
            {
                long long size = 0;
                const std::string h = sha256File (stage.path + "/" + rel, &size);
                if (h.empty()) return false;
                j["file"] = rel; j["sha256"] = h; j["bytes"] = size;
                entries.push_back ({ rel, stage.path + "/" + rel });
                return true;
            };

            const WavFormat fmt = options.bitDepth == 16 ? WavFormat::Pcm16 : options.bitDepth == 32 ? WavFormat::Float32 : WavFormat::Pcm24;
            json stems = json::array(), mix = nullptr;
            long clippedTotal = 0;
            const float steps = (float) plans->size() + 2.0f;
            int done = 0;
            std::vector<json> rendered;
            Frame lastAudible = 0;
            for (auto& sp : *plans)
            {
                if (ctx.cancel.load()) { error = "cancelado"; return false; }
                Engine offline;
                offline.prepare (p.sampleRate, 1024);
                for (size_t s = 0; s < sp.params.size(); ++s)
                {
                    TrackParams& tp = offline.trackParams ((int) s);
                    const ParamSnap& ps = sp.params[s];
                    tp.gain.store (ps.gain); tp.pan.store (ps.pan); tp.mute.store (ps.mute); tp.solo.store (ps.solo);
                    for (int k = 0; k < kMaxBuses; ++k) tp.send[k].store (ps.send[k]);
                }
                offline.setMasterGain (sp.master);
                offline.publish (std::move (sp.graph));
                AudioData out;
                out.sampleRate = p.sampleRate;
                out.allocate (2, total);
                if (! offline.renderOffline (0, out.channels[0].data(), out.channels[1].data(), total, &ctx.cancel)) { error = "cancelado"; return false; }
                float peak = 0;
                for (const auto& ch : out.channels)
                {
                    for (float v : ch) peak = std::max (peak, std::fabs (v));
                    Frame last = total;
                    while (last > lastAudible && std::fabs (ch[(size_t) (last - 1)]) < kSilence) --last;
                    lastAudible = std::max (lastAudible, last);
                }
                std::string err;
                WriteStats st;
                if (! writeWav (stage.path + "/" + sp.file, out, fmt, err, &st)) { error = "no se pudo escribir " + sp.file + ": " + err; return false; }
                clippedTotal += st.clippedSamples;
                json j = { { "name", sp.name }, { "role", sp.role }, { "channels", 2 }, { "sampleRate", p.sampleRate },
                           { "bitDepth", options.bitDepth }, { "offsetFrames", 0 }, { "peakDb", peak > 0 ? 20.0 * std::log10 (peak) : -144.0 },
                           { "clippedSamples", st.clippedSamples }, { "file", sp.file } };
                if (! sp.trackId.empty()) j["trackId"] = sp.trackId;
                if (sp.info.is_object()) j.update (sp.info);
                rendered.push_back (std::move (j));
                ctx.progress = (float) ++done / steps;
            }

            // La cola declarada por los efectos es prudente (una reverb larga declara varios segundos). Para no llenar
            // el paquete de silencio, todos los archivos se recortan en el MISMO punto: donde el último de ellos baja
            // de -100 dB, más un cuarto de segundo. Nunca antes del final del contenido. Siguen durando lo mismo.
            const Frame keep = std::min (total, std::max (p.contentEnd(), lastAudible + p.sampleRate / 4));
            const int bytesPerFrame = 2 * (options.bitDepth / 8);
            for (auto& j : rendered)
            {
                const std::string rel = j.value ("file", std::string());
                if (keep < total && ! truncateWavFile (stage.path + "/" + rel, keep, bytesPerFrame)) { error = "no se pudo ajustar " + rel; return false; }
                j["frames"] = keep;
                if (! resource (rel, j)) { error = "no se pudo leer " + rel; return false; }
                if (j.value ("role", std::string()) == "mix") { j["masterDb"] = p.masterDb; mix = j; } else stems.push_back (j);
            }
            const Frame total2 = keep;

            // MIDI
            json midi = nullptr;
            if (! smf.tracks.empty())
            {
                const std::string rel = "midi/" + safeName (p.name, "cancion") + ".mid";
                if (! writeText (stage.path + "/" + rel, std::string (smf.bytes.begin(), smf.bytes.end()))) { error = "no se pudo escribir el MIDI"; return false; }
                json tracks = json::array();
                for (const auto& t : smf.tracks)
                {
                    std::string realName = t.name;
                    for (const auto& tr : p.tracks) if (tr.id == t.trackId) realName = tr.name;
                    json jt = { { "trackId", t.trackId }, { "name", realName }, { "midiName", t.name }, { "smfTrack", t.index }, { "channel", t.channel + 1 }, { "notes", t.notes }, { "drums", t.drums } };
                    if (t.drums)
                    {
                        jt["drumBaseNote"] = kDrumBaseNote;
                        if (lanes.contains (t.trackId)) jt["laneNames"] = lanes[t.trackId];
                    }
                    tracks.push_back (jt);
                }
                midi = { { "format", 1 }, { "ticksPerQuarter", kPpq }, { "tracks", tracks }, { "notes", smf.totalNotes } };
                if (! resource (rel, midi)) { error = "no se pudo leer el MIDI"; return false; }
            }
            if (ctx.cancel.load()) { error = "cancelado"; return false; }

            // Proyecto original (.caucesound): el modelo tal como está en memoria + el audio que referencia.
            json project = nullptr;
            if (options.includeProject)
            {
                const std::string pj = stage.path + "/project/project.json";
                if (! writeText (pj, toJson (p))) { error = "no se pudo escribir el proyecto"; return false; }
                std::vector<ZipSource> files { { "project.json", pj } };
                json missing = json::array();
                for (const auto& s : p.sources)
                {
                    bool dup = false;
                    for (const auto& f : files) dup = dup || f.name == s.file;
                    if (dup) continue;
                    if (fileExists (projectDir + "/" + s.file)) files.push_back ({ s.file, projectDir + "/" + s.file });
                    else missing.push_back (s.file);
                }
                const std::string rel = "project/" + safeName (p.name, "cancion") + ".caucesound";
                const ArchiveResult ar = writeStoredZip (files, stage.path + "/" + rel);
                if (! ar.ok) { error = "no se pudo empaquetar el proyecto: " + ar.error; return false; }
                removeFile (pj);
                project = { { "formatVersion", p.version }, { "audioFiles", ar.files - 1 }, { "missingAudio", missing } };
                if (! resource (rel, project)) { error = "no se pudo leer el proyecto empaquetado"; return false; }
            }
            ctx.progress = (steps - 1.0f) / steps;

            json notTranslated = json::array ({
                "Los instrumentos Cauce Synth y Cauce Drums no existen fuera de CAUCESOUND: el MIDI lleva las notas y los stems llevan su sonido ya renderizado.",
                "Los efectos de CAUCESOUND no se convierten en plugins del DAW: van aplicados en los stems (modo con efectos) o listados como no aplicados (modo sin efectos).",
                "El proyecto tiene un solo tempo y un solo compás. No hay automatizaciones." });
            if (! options.wet) notTranslated.push_back ("Modo sin efectos: no se exportan retornos de bus; los envíos de cada pista están en su entrada del manifiesto.");
            if (anySolo) notTranslated.push_back ("Había pistas en solo: la mezcla incluida refleja ese solo; los stems se exportan todos.");

            json manifest = {
                { "format", "cauce-daw" }, { "formatVersion", 1 }, { "generator", "CAUCESOUND" }, { "createdUtc", isoNow() },
                { "mode", options.wet ? "wet" : "dry" },
                { "song", { { "id", p.id }, { "name", p.name }, { "bpm", p.bpm }, { "beatsPerBar", p.beatsPerBar }, { "beatUnit", 4 },
                            { "sampleRate", p.sampleRate }, { "contentFrames", p.contentEnd() }, { "frames", total2 },
                            { "seconds", (double) total2 / p.sampleRate }, { "masterDb", p.masterDb }, { "masterAppliedToStems", false } } },
                { "alignment", "Todos los WAV empiezan en el compás 1 (frame 0) y duran lo mismo: colócalos al inicio del proyecto del DAW." },
                { "stems", stems }, { "mix", mix }, { "midi", midi }, { "project", project }, { "skipped", skipped },
                { "notTranslated", notTranslated } };
            if (! writeText (stage.path + "/manifest.json", manifest.dump (2))) { error = "no se pudo escribir el manifiesto"; return false; }
            entries.push_back ({ "manifest.json", stage.path + "/manifest.json" });

            std::string readme =
                "CAUCESOUND - Paquete para DAW\r\n=============================\r\n\r\n"
                "Cancion: " + p.name + "\r\nTempo: " + std::to_string (p.bpm).substr (0, std::to_string (p.bpm).find ('.') + 3) + " BPM   Compas: "
                + std::to_string (p.beatsPerBar) + "/4   Frecuencia: " + std::to_string (p.sampleRate) + " Hz\r\n\r\n"
                "Como usarlo (FL Studio, Reaper, Cubase, Ableton...):\r\n"
                "1. Pon el tempo del proyecto del DAW en el valor de arriba.\r\n"
                "2. Arrastra todos los WAV de la carpeta stems/ al inicio (compas 1). Todos empiezan en el mismo punto y duran lo mismo.\r\n"
                "3. Si quieres cambiar notas o sonidos, importa midi/*.mid: cada pista lleva su nombre. La bateria va en el canal 10.\r\n"
                "4. mix/ contiene la mezcla tal como sonaba en CAUCESOUND, para comparar.\r\n"
                "5. project/ contiene el proyecto original (.caucesound) para volver a abrirlo en CAUCESOUND.\r\n\r\n";
            readme += options.wet
                ? "Modo: CON EFECTOS. Cada pista lleva sus efectos, su volumen y su panorama. Los efectos compartidos (buses) van en archivos aparte llamados retorno_*: no los quites o la mezcla perdera su reverb. Sumando todos los stems obtienes la mezcla (sin el volumen de master).\r\n"
                : "Modo: SIN EFECTOS. Cada pista sale limpia, a 0 dB y centrada, para mezclar desde cero. manifest.json indica el volumen, el panorama y los efectos que tenia cada una.\r\n";
            readme += "\r\nLimites: no es un proyecto nativo de ningun DAW. Los instrumentos y efectos de CAUCESOUND no se convierten en plugins.\r\n"
                      "manifest.json lista cada archivo con su hash SHA-256 para comprobar que llego completo.\r\n";
            if (! writeText (stage.path + "/README.txt", readme)) { error = "no se pudo escribir el README"; return false; }
            entries.push_back ({ "README.txt", stage.path + "/README.txt" });

            if (ctx.cancel.load()) { error = "cancelado"; return false; }
            const ArchiveResult zr = writeStoredZip (entries, outPath);
            if (! zr.ok) { error = "no se pudo crear el paquete: " + zr.error; return false; }
            long long zipBytes = 0;
            const std::string zipHash = sha256File (outPath, &zipBytes);
            ctx.progress = 1.0f;
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            result = json { { "path", outPath }, { "files", zr.files }, { "bytes", zipBytes }, { "sha256", zipHash }, { "stems", (int) stems.size() },
                            { "midiTracks", (int) smf.tracks.size() }, { "midiNotes", smf.totalNotes }, { "clippedSamples", clippedTotal },
                            { "mode", options.wet ? "wet" : "dry" }, { "seconds", secs }, { "songSeconds", (double) total2 / p.sampleRate } }.dump();
            return true;
        });
    if (jobId.empty()) return OpResult::fail ("hay demasiados procesos en cola: espera a que termine alguno");
    OpResult r;
    r.id = jobId;
    return r;
}

OpResult Session::exportMidi (const std::string& outPath)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (outPath.empty() || ! fs::path (outPath).is_absolute()) return OpResult::fail ("la ruta de exportación debe ser absoluta");
    const SmfExport smf = writeSmf (proj);
    if (smf.tracks.empty()) return OpResult::fail ("no hay notas que exportar");
    std::string err;
    if (! atomicWriteFile (outPath, std::string (smf.bytes.begin(), smf.bytes.end()), err, false)) return OpResult::fail (err);
    OpResult r;
    r.id = std::to_string (smf.totalNotes);
    return r;
}
} // namespace cs
