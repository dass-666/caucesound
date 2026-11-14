#include "cs/Api.h"
#include "cs/FxChains.h"
#include "cs/SoloBridge.h"
#include "cs/Archive.h"

#include "cs/Instruments.h"
#include "cs/Packs.h"
#include "cs/Project.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cs
{
using nlohmann::json;

namespace
{
json result (const OpResult& r)
{
    json j = { { "ok", r.ok } };
    if (! r.id.empty()) j["id"] = r.id;
    if (! r.error.empty()) j["error"] = r.error;
    return j;
}

json listProjects (const std::string& root)
{
    json arr = json::array();
    for (const auto& name : listDir (root))
    {
        const std::string folder = root + "/" + name;
        std::string text;
        if (! readFile (folder + "/project.json", text) && ! readFile (folder + "/project.json.bak", text)) continue;
        ProjectModel p;
        std::string err;
        json item = { { "folder", folder } };
        if (fromJson (text, p, err))
        {
            item["name"] = p.name;
            item["id"] = p.id;
            item["modifiedAt"] = p.modifiedAt;
            item["tracks"] = p.tracks.size();
            item["bpm"] = p.bpm;
            item["template"] = p.templateId;
            // Miniatura: por pista, color y tramos ocupados (fracción 0..1 de la duración del proyecto).
            const double total = (double) std::max<Frame> (p.contentEnd(), 1);
            json thumb = json::array();
            for (size_t ti = 0; ti < p.tracks.size() && ti < 8; ++ti)
            {
                const auto& t = p.tracks[ti];
                json spans = json::array();
                for (const auto& c : t.clips) if (spans.size() < 16) spans.push_back ({ c.start / total, (c.start + c.length) / total });
                for (const auto& c : t.midiClips)
                    if (spans.size() < 16) spans.push_back ({ p.ticksToFrames (c.start) / total, p.ticksToFrames (c.start + c.length) / total });
                thumb.push_back ({ { "color", t.color }, { "kind", t.kind }, { "spans", spans } });
            }
            item["thumb"] = thumb;
        }
        else
        {
            item["name"] = name;
            item["error"] = err;
        }
        arr.push_back (item);
    }
    return arr;
}
} // namespace

namespace
{
json pluginDescriptors();
json catalog()
{
    json presets = json::array();
    for (const auto& p : synthPresets()) presets.push_back ({ { "id", p.id }, { "name", p.name }, { "category", p.category } });
    json kits = json::array();
    for (const auto& k : drumKits()) kits.push_back ({ { "id", k.id }, { "name", k.name }, { "lanes", k.laneNames } });
    auto params = [] (const char* type)
    {
        json arr = json::array();
        if (auto p = createProcessor (type))
            for (const auto& i : p->parameters())
                arr.push_back ({ { "id", i.id }, { "name", i.name }, { "min", i.minValue }, { "max", i.maxValue }, { "default", i.defaultValue } });
        return arr;
    };
    json fx = json::array();
    for (const auto& t : effectTypes())
    {
        json e { { "type", t }, { "params", params (t.c_str()) } };
        if (const EffectInfo* i = findEffectInfo (t))
        {
            e["category"] = i->category; e["name"] = i->name; e["nameEn"] = i->nameEn; e["description"] = i->description; e["descriptionEn"] = i->descriptionEn;
            if (auto p = createProcessor (t)) { p->prepare (48000, 1024); e["latencySamples"] = p->latencySamples(); }
        }
        fx.push_back (e);
    }
    // Cadenas completas (Mixer Pro): solo las que usan efectos disponibles.
    json chains = json::array();
    for (const auto& c : chainPresets())
    {
        json steps = json::array();
        for (const auto& st : c.steps) steps.push_back (st.type);
        chains.push_back ({ { "id", c.id }, { "name", c.name }, { "nameEn", c.nameEn }, { "description", c.description }, { "descriptionEn", c.descriptionEn },
                            { "suits", c.suits }, { "steps", steps } });
    }
    json tpl = json::array();
    for (const auto& t : projectTemplates()) tpl.push_back ({ { "id", t.id }, { "bpm", t.bpm }, { "kit", t.kitId }, { "tracks", t.tracks } });
    return { { "ok", true }, { "ppq", kPpq }, { "synthPresets", presets }, { "drumKits", kits }, { "synthParams", params ("cs.synth") },
             { "drumParams", params ("cs.drums") }, { "samplerParams", params ("cs.sampler") }, { "effects", fx }, { "fxChains", chains }, { "templates", tpl },
             // Plugins integrados: solo se anuncian los que están compilados (la interfaz no muestra los demás).
             { "plugins", pluginDescriptors() } };
}

// Contrato de plugins (docs/PLUGIN_CONTRACT.md): descriptor de cada plugin integrado. Solo aparecen los compilados.
json pluginDescriptors()
{
    json arr = json::array();
    if (solo::available())
        arr.push_back ({ { "id", "solo.ai" }, { "name", "Solo AI" }, { "version", solo::pinnedVersion() }, { "category", "generator" },
                         { "kind", "offline" }, { "realtime", false }, { "latencySamples", 0 }, { "wantsMidi", false }, { "wantsTransport", true },
                         { "output", "midi" }, { "appliesTo", "song" }, { "cancellable", false } });
    if (vocal::available())
        arr.push_back ({ { "id", "vocalshift" }, { "name", "VocalShift" }, { "version", vocal::pinnedVersion() }, { "category", "effect" },
                         { "kind", "offline" }, { "realtime", false }, { "latencyMs", 150 }, { "wantsMidi", false }, { "wantsTransport", true },
                         { "output", "audio" }, { "appliesTo", "audioClip" }, { "cancellable", true }, { "maxSeconds", vocal::kMaxClipSeconds },
                         { "presets", (int) vocal::presets().size() } });
    return arr;
}

// Entero desde JSON sin conversiones fuera de rango (1e30 -> int es comportamiento indefinido en C++).
long long safeInt (const json& v)
{
    if (! v.is_number()) throw std::invalid_argument ("se esperaba un número");
    const double d = v.get<double>();
    if (! std::isfinite (d) || std::fabs (d) > 1e9) throw std::invalid_argument ("número fuera de rango");
    return (long long) d;
}

std::vector<MidiNote> parseNotes (const json& arr)
{
    std::vector<MidiNote> notes;
    if (! arr.is_array()) return notes;
    for (const auto& jn : arr)
    {
        MidiNote n;
        if (jn.is_array())
        {
            n.key = (int) safeInt (jn.at (0)); n.velocity = (int) safeInt (jn.at (1)); n.channel = (int) safeInt (jn.at (2));
            n.start = (Tick) safeInt (jn.at (3)); n.length = (Tick) safeInt (jn.at (4));
        }
        else
        {
            n.key = (int) safeInt (jn.at ("key")); n.velocity = jn.contains ("velocity") ? (int) safeInt (jn["velocity"]) : 100;
            n.channel = jn.contains ("channel") ? (int) safeInt (jn["channel"]) : 0;
            n.start = (Tick) safeInt (jn.at ("start")); n.length = (Tick) safeInt (jn.at ("length"));
        }
        notes.push_back (n);
    }
    return notes;
}

// Paquetes de sonidos instalados (misión 0.9), para el explorador de instrumentos.
json packsJson()
{
    json arr = json::array();
    for (const auto& p : packLibrary().list())
    {
        const auto& m = p.manifest;
        json instr = json::array();
        for (const auto& i : m.instruments)
            instr.push_back ({ { "key", m.id + ":" + i.id }, { "id", i.id }, { "name", i.name }, { "family", i.family }, { "kind", i.kind },
                               { "description", i.description }, { "origin", i.origin }, { "tags", i.tags }, { "genres", i.genres },
                               { "lanes", i.laneNames }, { "previewKey", i.previewKey }, { "lowKey", i.lowKey }, { "highKey", i.highKey },
                               { "mono", i.mono } });
        json src = json::array();
        for (const auto& so : m.sources) src.push_back ({ { "name", so.name }, { "author", so.author }, { "url", so.url }, { "license", so.license } });
        arr.push_back ({ { "id", m.id }, { "version", m.version }, { "name", m.name }, { "author", m.author }, { "description", m.description },
                         { "license", m.license }, { "bytes", m.totalBytes() }, { "factory", p.factory }, { "complete", p.complete },
                         { "problems", p.problems }, { "sources", src }, { "instruments", instr } });
    }
    return arr;
}

// Proyectos de la carpeta raíz que usan instrumentos de un paquete (aviso antes de borrarlo).
json packUsage (const std::string& root, const std::string& packId)
{
    json arr = json::array();
    const std::string needle = "\"" + packId + ":";
    for (const auto& name : listDir (root))
    {
        std::string text;
        const std::string folder = root + "/" + name;
        if (! readFile (folder + "/project.json", text)) continue;
        // El estado del instrumento va como texto JSON escapado (\"cauce.x:y\"): en ambos casos aparece "<id>:.
        if (text.find (needle) == std::string::npos)
            continue;
        ProjectModel pm;
        std::string err;
        arr.push_back ({ { "folder", folder }, { "name", fromJson (text, pm, err) ? pm.name : name } });
    }
    return arr;
}

bool insideRoot (const std::string& root, const std::string& folder)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto r = fs::weakly_canonical (root, ec).string(), f = fs::weakly_canonical (folder, ec).string();
    return ! r.empty() && f.size() > r.size() + 1 && f.compare (0, r.size(), r) == 0 && f[r.size()] == '/';
}
} // namespace

std::string handleCommand (Session& s, Engine& e, const std::string& request)
{
    const json q = json::parse (request, nullptr, false);
    if (q.is_discarded() || ! q.is_object() || ! q.contains ("cmd") || ! q["cmd"].is_string())
        return json { { "ok", false }, { "error", "petición no válida" } }.dump();
    try
    {
        const std::string cmd = q["cmd"].get<std::string>();
        // Las rutas de archivo deben ser absolutas: la app siempre las pasa así, y una relativa escribiría en el
        // directorio actual del proceso (lo detectó el fuzzing: un id de pista usado como "path" creaba un WAV ahí).
        auto str = [&] (const char* k)
        {
            std::string v = q.value (k, std::string());
            static const char* pathKeys[] = { "folder", "path", "out", "archive", "dest", "root", "factoryDir", "userDir" };
            for (const char* pk : pathKeys)
                if (std::strcmp (k, pk) == 0 && ! v.empty() && ! std::filesystem::path (v).is_absolute())
                    throw std::invalid_argument (std::string (k) + " debe ser una ruta absoluta");
            return v;
        };
        // Un argumento presente con tipo equivocado o fuera de rango es un error (antes se tomaba el valor por
        // defecto en silencio: p. ej. {"bpm":"rápido"} ponía el tempo a 120). Límite 1e9: cabe en int y en frames
        // (5,8 h a 48 kHz) y evita conversiones de double a entero fuera de rango (comportamiento indefinido).
        auto num = [&] (const char* k, double d = 0)
        {
            if (! q.contains (k) || q[k].is_null()) return d;
            if (! q[k].is_number()) throw std::invalid_argument (std::string (k) + " debe ser un número");
            const double v = q[k].get<double>();
            if (! std::isfinite (v) || std::fabs (v) > 1e9) throw std::invalid_argument (std::string (k) + " fuera de rango");
            return v;
        };
        auto frame = [&] (const char* k, Frame d = 0) { return q.contains (k) && ! q[k].is_null() ? (Frame) num (k) : d; };
        auto flag = [&] (const char* k) { return q.value (k, false); };

        if (cmd == "state") return s.stateJson();
        if (cmd == "listProjects") return json { { "ok", true }, { "projects", listProjects (str ("root")) } }.dump();
        if (cmd == "createProject")
        {
            // 0.10: "style": 2 (o "template": "trap@2") crea la plantilla en modo «Signature 2». Sin eso, la de siempre.
            std::string tpl = str ("template");
            if (q.value ("style", 1) == 2 && tpl.find ('@') == std::string::npos && ! tpl.empty() && tpl != "empty") tpl += "@2";
            json j = result (s.createProject (str ("folder"), str ("name"), 48000, tpl));
            if (j.value ("ok", false)) j["notes"] = s.templateNotes();
            return j.dump();
        }
        // 0.10, experimental: medición de VocalShift en vivo (no toca el proyecto ni el audio) e interruptor de pruebas.
        if (cmd == "benchLiveVocal") return vocal::benchLiveInsert (num ("seconds", 5));
        if (cmd == "setExperimental") { s.setExperimentalLiveVocal (q.value ("liveVocal", false)); return json { { "ok", true }, { "liveVocal", s.experimentalLiveVocal() } }.dump(); }
        if (cmd == "setHumanize") return result (s.setHumanize ((float) num ("amount"))).dump();
        if (cmd == "catalog") return catalog().dump();
        if (cmd == "packs") return json { { "ok", true }, { "packs", packsJson() } }.dump();
        if (cmd == "configurePacks")
        {
            packLibrary().configure (str ("factoryDir"), str ("userDir"));
            return json { { "ok", true }, { "packs", packsJson() } }.dump();
        }
        if (cmd == "importPack")
        {
            const PackImportResult r = packLibrary().importZip (str ("path"), flag ("replace"));
            json j = { { "ok", r.ok }, { "packId", r.packId }, { "version", r.version }, { "files", r.files }, { "bytes", r.bytes }, { "replaced", r.replaced } };
            if (! r.ok) j["error"] = r.error;
            return j.dump();
        }
        if (cmd == "packUsage") return json { { "ok", true }, { "projects", packUsage (str ("root"), str ("packId")) } }.dump();
        if (cmd == "removePack")
        {
            const std::string id = str ("packId");
            // El proyecto abierto no puede perder su sonido sin aviso: la interfaz debe cerrar o cambiar antes.
            for (const auto& k : s.usedPackInstruments())
                if (k.rfind (id + ":", 0) == 0) return result (OpResult::fail ("el proyecto abierto usa este paquete")).dump();
            std::string err;
            if (! packLibrary().remove (id, err)) return result (OpResult::fail (err)).dump();
            return result ({}).dump();
        }
        if (cmd == "verifyPack")
        {
            std::vector<std::string> bad;
            std::string err;
            const bool ok = packLibrary().verify (str ("packId"), bad, err);
            json j = { { "ok", ok }, { "bad", bad } };
            if (! err.empty()) j["error"] = err;
            return j.dump();
        }
        if (cmd == "autoMixSuggest")
        {
            json arr = json::array();
            for (const auto& a : s.autoMixSuggest (num ("seconds", 16)))
                arr.push_back ({ { "trackId", a.trackId }, { "name", a.name }, { "role", a.role }, { "reason", a.reason }, { "measuredDb", a.measuredDb },
                                 { "targetDb", a.targetDb }, { "volumeDb", a.volumeDb }, { "suggestedVolumeDb", a.suggestedVolumeDb }, { "pan", a.pan },
                                 { "suggestedPan", a.suggestedPan }, { "change", a.change } });
            return json { { "ok", s.isOpen() }, { "suggestions", arr }, { "canRevert", s.canRevertAutoMix() }, { "rules", "reglas fijas por papel y estilo (no IA): docs/AUTOMIX.md" } }.dump();
        }
        if (cmd == "autoMixApply")
        {
            std::vector<Session::MixChange> ch;
            for (const auto& c : q.value ("changes", json::array()))
                ch.push_back ({ c.at ("trackId").get<std::string>(), (float) c.at ("volumeDb").get<double>(), (float) c.at ("pan").get<double>() });
            return result (s.applyAutoMix (ch)).dump();
        }
        if (cmd == "autoMixRevert") return result (s.revertAutoMix()).dump();
        if (cmd == "setSamplerInstrument") return result (s.setSamplerInstrument (str ("trackId"), str ("key"))).dump();
        if (cmd == "setDrumSampleKit") return result (s.setDrumSampleKit (str ("trackId"), str ("key"))).dump();
        if (cmd == "previewInstrument")
        {
            json j = result (s.setPreviewInstrument (str ("key")));
            j["slot"] = s.previewSlot();
            j.erase ("id");
            return j.dump();
        }
        if (cmd == "renameProject" || cmd == "duplicateProject")
        {
            // Opera sobre proyectos cerrados (desde la pantalla de inicio). root delimita lo que se puede tocar.
            const std::string root = str ("root"), folder = str ("folder");
            if (! insideRoot (root, folder)) return result (OpResult::fail ("carpeta fuera de los proyectos")).dump();
            if (s.isOpen() && s.folder() == folder) s.close();
            std::string text, err;
            if (! readFile (folder + "/project.json", text)) return result (OpResult::fail ("no se encontró project.json")).dump();
            json pj = json::parse (text, nullptr, false);
            if (pj.is_discarded()) return result (OpResult::fail ("proyecto dañado")).dump();
            if (cmd == "renameProject")
            {
                pj["name"] = str ("name");
                if (! atomicWriteFile (folder + "/project.json", pj.dump (2), err, true)) return result (OpResult::fail (err)).dump();
                return json { { "ok", true } }.dump();
            }
            const std::string dest = str ("dest");
            if (! insideRoot (root, dest) || fileExists (dest)) return result (OpResult::fail ("destino no válido")).dump();
            std::error_code ec;
            std::filesystem::copy (folder, dest, std::filesystem::copy_options::recursive, ec);
            if (ec) { std::filesystem::remove_all (dest, ec); return result (OpResult::fail ("no se pudo copiar el proyecto (¿espacio?)")).dump(); }
            pj["name"] = str ("name");
            pj["id"] = newId ("prj");
            if (! atomicWriteFile (dest + "/project.json", pj.dump (2), err, false)) return result (OpResult::fail (err)).dump();
            removeFile (dest + "/project.json.bak");
            return json { { "ok", true }, { "folder", dest } }.dump();
        }
        if (cmd == "exportProjectArchive")
        {
            // Proyecto completo en un .caucesound (ZIP). Si es el proyecto abierto, se guarda antes.
            const std::string root = str ("root"), folder = str ("folder");
            if (! insideRoot (root, folder)) return result (OpResult::fail ("carpeta fuera de los proyectos")).dump();
            if (s.isOpen() && s.folder() == folder)
                if (auto r = s.save(); ! r.ok) return result (r).dump();
            const auto a = exportProjectArchive (folder, str ("out"));
            if (! a.ok) return result (OpResult::fail (a.error)).dump();
            return json { { "ok", true }, { "files", a.files }, { "bytes", a.bytes }, { "name", a.name } }.dump();
        }
        if (cmd == "importProjectArchive")
        {
            const std::string root = str ("root"), dest = str ("dest");
            if (! insideRoot (root, dest)) return result (OpResult::fail ("destino fuera de los proyectos")).dump();
            const auto a = importProjectArchive (str ("archive"), dest);
            if (! a.ok) return result (OpResult::fail (a.error)).dump();
            return json { { "ok", true }, { "folder", dest }, { "files", a.files }, { "bytes", a.bytes }, { "name", a.name } }.dump();
        }
        if (cmd == "openProject")
        {
            LoadReport rep;
            json j = result (s.openProject (str ("folder"), &rep));
            j["missingSources"] = rep.missingSources;
            j["recoveredRecordings"] = rep.recoveredRecordings;
            j["usedBackup"] = rep.usedBackup;
            return j.dump();
        }
        if (cmd == "closeProject") { s.close(); return json { { "ok", true } }.dump(); }
        if (cmd == "save") return result (s.save()).dump();
        if (cmd == "addTrack") return result (s.addTrack (str ("name"), q.value ("kind", std::string ("audio")))).dump();
        if (cmd == "addInsert") return result (s.addInsert (str ("trackId"), str ("type"))).dump();
        if (cmd == "removeInsert") return result (s.removeInsert (str ("insertId"))).dump();
        if (cmd == "setInsertParam") return result (s.setInsertParam (str ("insertId"), str ("param"), (float) num ("value"))).dump();
        if (cmd == "setInsertBypass") return result (s.setInsertBypass (str ("insertId"), q.value ("bypass", false))).dump();
        if (cmd == "routeDrumLane") return result (s.routeDrumLane (str ("trackId"), (int) num ("lane"), flag ("own"))).dump();
        if (cmd == "applyFxChain") return result (s.applyFxChain (str ("trackId"), str ("preset"), flag ("replace"))).dump();
        if (cmd == "moveInsert") return result (s.moveInsert (str ("insertId"), (int) num ("index"))).dump();
        if (cmd == "insertMeter") return json { { "ok", true }, { "grDb", s.insertMeter (str ("insertId")) } }.dump();
        if (cmd == "setInstrumentParam") return result (s.setInstrumentParam (str ("trackId"), str ("param"), (float) num ("value"))).dump();
        if (cmd == "loadPreset") return result (s.loadSynthPreset (str ("trackId"), str ("preset"))).dump();
        if (cmd == "addPattern") return result (s.addPattern (str ("trackId"), str ("copyFrom"))).dump();
        // 0.9.2: las tres devuelven además "variation" (informe: versión, partes cambiadas y cuánto, candados).
        if (cmd == "mutatePart" || cmd == "regenerateBeat" || cmd == "varyPart")
        {
            OpResult r;
            if (cmd == "mutatePart") r = s.mutatePart (q.value ("part", std::string ("all")), (uint32_t) q.value ("seed", (int64_t) 1));
            else if (cmd == "regenerateBeat") r = s.regenerateBeat ((uint32_t) q.value ("seed", (int64_t) 1));
            else
            {
                const bool setE = q.contains ("energy");
                r = s.varyPart (q.value ("part", std::string ("all")), (uint32_t) q.value ("seed", (int64_t) 0), setE ? (float) num ("energy") : 0.0f, setE);
            }
            json j = result (r);
            if (r.ok)
            {
                const json rep = json::parse (s.lastVariationReport(), nullptr, false);
                if (! rep.is_discarded()) j["variation"] = rep;
            }
            return j.dump();
        }
        if (cmd == "soloMelody" || cmd == "soloProposals")
        {
            // Solo AI: la petición se prepara con la sesión bloqueada, la composición se hace SIN el cerrojo (no frena
            // otros comandos ni el audio) y el resultado entra como pista nueva, si el proyecto sigue siendo el mismo.
            solo::MelodyRequest req;
            const int startBar = (int) num ("startBar", 0), bars = (int) num ("bars", 8);
            auto pr = s.soloRequest (startBar, bars, (int) num ("keyRoot", -1), flag ("minor"), req);
            if (! pr.ok) return result (pr).dump();
            req.type = q.value ("type", std::string ("chorus"));
            req.character = q.value ("character", std::string ("easy"));
            req.freedom = num ("freedom", 35);
            req.proposal = (int) num ("proposal", 0);
            req.seed = (uint64_t) safeInt (q.value ("seed", json (1)));
            req.variation = (uint64_t) safeInt (q.value ("variation", json (0)));
            req.endingSeed = (uint64_t) safeInt (q.value ("endingSeed", json (0)));
            req.endingBars = std::clamp ((int) num ("endingBars", 2), 1, std::max (1, bars));
            auto describe = [] (const solo::MelodyResult& m)
            {
                Tick end = 0;
                int lo = 127, hi = 0;
                for (const auto& n : m.notes) { end = std::max (end, n.start + n.length); lo = std::min (lo, n.key); hi = std::max (hi, n.key); }
                return json { { "name", m.name }, { "description", m.description }, { "notes", m.notes.size() }, { "inScale", m.inScale },
                              { "strongChordTones", m.strongChordTones }, { "similarity", m.similarity }, { "lowKey", lo }, { "highKey", hi }, { "lengthTicks", end } };
            };
            if (cmd == "soloProposals")
            {
                // Las cuatro alternativas A-D de la misma familia. No toca el proyecto: son reproducibles por semilla, así
                // que elegir una después (soloMelody con la misma semilla y su número) da exactamente esas notas.
                const auto fam = solo::proposeFamily (req);
                if (! fam.ok) return json { { "ok", false }, { "error", fam.error } }.dump();
                json arr = json::array();
                for (const auto& p : fam.proposals) arr.push_back (describe (p));
                return json { { "ok", true }, { "proposals", arr }, { "diversity", fam.diversity }, { "keyRoot", req.keyRoot }, { "minor", req.minor } }.dump();
            }
            const std::string projectId = s.model().id;
            const auto mr = solo::generateMelody (req);
            if (! mr.ok) return json { { "ok", false }, { "error", mr.error } }.dump();
            const auto bar = (Tick) kPpq * s.model().beatsPerBar;
            const std::string name = "Solo AI · " + mr.name;
            // Frase final nueva sobre una pista existente: se conserva lo anterior al corte (también lo editado a mano).
            const Tick keepBefore = req.endingSeed != 0 && ! str ("replaceTrackId").empty() ? (Tick) (bars - req.endingBars) * bar : 0;
            auto r = s.addSoloTrack (projectId, (Tick) startBar * bar, name, mr.notes, q.value ("preset", std::string ("bright_lead")), str ("replaceTrackId"), keepBefore);
            json j = result (r);
            j.update (describe (mr));
            j["name"] = name;
            j["millis"] = mr.millis;
            j["keyRoot"] = req.keyRoot;
            j["minor"] = req.minor;
            return j.dump();
        }
        if (cmd == "soloTransform")
        {
            json j = result (s.soloTransform (str ("trackId"), str ("op")));
            if (j.value ("ok", false)) { j["notes"] = std::atoi (j.value ("id", std::string ("0")).c_str()); j.erase ("id"); }
            return j.dump();
        }
        if (cmd == "soloAnalyze")
        {
            json j = result (s.soloAnalyze (str ("clipId")));
            if (j.value ("ok", false)) j["jobId"] = j.value ("id", std::string());
            return j.dump();
        }
        if (cmd == "setHarmony")
        {
            std::vector<solo::ChordSpec> chords;
            for (const auto& c : q.value ("chords", json::array()))
                chords.push_back ({ (int) safeInt (c.value ("root", json (0))), (int) safeInt (c.value ("quality", json (0))) });
            return result (s.setHarmony ((int) num ("keyRoot", -1), flag ("minor"), chords, num ("chordsBpm", 0), q.value ("source", std::string ("manual")))).dump();
        }
        if (cmd == "vocalPresets")
        {
            json arr = json::array();
            for (const auto& p : vocal::presets())
                arr.push_back ({ { "id", p.id }, { "name", p.name }, { "description", p.description }, { "params", json::parse (p.params.toJson()) } });
            return json { { "ok", true }, { "presets", arr } }.dump();
        }
        if (cmd == "vocalProcess")
        {
            vocal::Params p;
            if (! vocal::Params::fromJson (q.value ("params", json::object()).dump(), p)) return result (OpResult::fail ("parámetros no válidos")).dump();
            json j = result (s.vocalProcess (str ("clipId"), p));
            if (j.value ("ok", false)) j["jobId"] = j.value ("id", std::string());
            return j.dump();
        }
        if (cmd == "vocalPreview")
        {
            vocal::Params p;
            if (! vocal::Params::fromJson (q.value ("params", json::object()).dump(), p)) return result (OpResult::fail ("parámetros no válidos")).dump();
            json j = result (s.vocalPreview (str ("clipId"), p, q.contains ("from") ? (Frame) safeInt (q["from"]) : 0, q.contains ("length") ? (Frame) safeInt (q["length"]) : 0));
            if (j.value ("ok", false))
            {
                const std::string id = j.value ("id", std::string());
                j["cached"] = id == "cached";
                if (id != "cached") j["jobId"] = id;
                j.erase ("id");
            }
            return j.dump();
        }
        if (cmd == "previewListen") return result (s.setPreviewListen (str ("clipId"), flag ("on"))).dump();
        if (cmd == "clipVersion") return result (s.setClipVersion (str ("clipId"), flag ("original"), q.contains ("match") && q["match"].is_boolean() ? (q["match"].get<bool>() ? 1 : 0) : -1)).dump();
        if (cmd == "discardProcess") return result (s.discardProcess (str ("clipId"))).dump();
        if (cmd == "jobStatus")
        {
            JobInfo info;
            if (! s.jobRunner().info (str ("jobId"), info)) return json { { "ok", false }, { "error", "trabajo no encontrado" } }.dump();
            json j { { "ok", true }, { "jobId", info.id }, { "type", info.type }, { "state", info.state }, { "progress", info.progress },
                     { "targetId", info.targetId }, { "error", info.error } };
            if (! info.result.empty()) j["result"] = json::parse (info.result, nullptr, false);
            return j.dump();
        }
        if (cmd == "jobCancel") return json { { "ok", s.jobRunner().cancel (str ("jobId")) } }.dump();
        if (cmd == "setPartLocked") return result (s.setPartLocked (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setRoll") return result (s.setPatternRoll (str ("trackId"), str ("patternId"), (int) num ("lane"), (int) num ("step"), (int) num ("roll"))).dump();
        if (cmd == "setStep") return result (s.setPatternStep (str ("trackId"), str ("patternId"), (int) num ("lane"), (int) num ("step"), (int) num ("velocity"))).dump();
        if (cmd == "setPatternLength") return result (s.setPatternLength (str ("trackId"), str ("patternId"), (int) num ("steps", 16))).dump();
        if (cmd == "setSwing") return result (s.setPatternSwing (str ("trackId"), str ("patternId"), (float) num ("swing"))).dump();
        if (cmd == "setPatternLanes") return result (s.setPatternLanes (str ("trackId"), str ("patternId"), [&] {
                std::vector<std::vector<int>> lanes;
                for (const auto& lane : q.at ("lanes")) { lanes.emplace_back(); for (const auto& v : lane) lanes.back().push_back ((int) safeInt (v)); }
                return lanes; }())).dump();
        if (cmd == "renamePattern") return result (s.renamePattern (str ("trackId"), str ("patternId"), str ("name"))).dump();
        if (cmd == "addMidiClip") return result (s.addMidiClip (str ("trackId"), (Tick) num ("start"), (Tick) num ("length", 4 * kPpq * 4), str ("patternId"))).dump();
        if (cmd == "moveMidiClip") return result (s.moveMidiClip (str ("clipId"), (Tick) num ("start"), str ("trackId"))).dump();
        if (cmd == "resizeMidiClip") return result (s.resizeMidiClip (str ("clipId"), (Tick) num ("start"), (Tick) num ("length"))).dump();
        if (cmd == "deleteMidiClip") return result (s.deleteMidiClip (str ("clipId"))).dump();
        if (cmd == "duplicateMidiClip") return result (s.duplicateMidiClip (str ("clipId"))).dump();
        if (cmd == "setClipPattern") return result (s.setMidiClipPattern (str ("clipId"), str ("patternId"))).dump();
        if (cmd == "setClipNotes") return result (s.setClipNotes (str ("clipId"), parseNotes (q.value ("notes", json::array())))).dump();
        if (cmd == "quantizeClip") return result (s.quantizeClip (str ("clipId"), (Tick) num ("grid", kStepTicks), (float) num ("strength", 1.0), flag ("lengths"))).dump();
        if (cmd == "setLoop") return result (s.setLoop (flag ("on"), (Tick) num ("start"), (Tick) num ("end", 16 * kPpq))).dump();
        if (cmd == "removeTrack") return result (s.removeTrack (str ("trackId"))).dump();
        if (cmd == "renameTrack") return result (s.renameTrack (str ("trackId"), str ("name"))).dump();
        if (cmd == "setVolume") return result (s.setTrackVolumeDb (str ("trackId"), (float) num ("db"))).dump();
        if (cmd == "setSend") return result (s.setTrackSend (str ("trackId"), str ("busId"), (float) num ("level"))).dump();
        if (cmd == "setPan") return result (s.setTrackPan (str ("trackId"), (float) num ("pan"))).dump();
        if (cmd == "setMute") return result (s.setTrackMute (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setSolo") return result (s.setTrackSolo (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setArmed") return result (s.setTrackArmed (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setMaster") return result (s.setMasterDb ((float) num ("db"))).dump();
        if (cmd == "importWav") return result (s.importWav (str ("path"), str ("trackId"), frame ("at", -1), str ("name"))).dump();
        if (cmd == "moveClip") return result (s.moveClip (str ("clipId"), frame ("start"), str ("trackId"))).dump();
        if (cmd == "trimClip") return result (s.trimClip (str ("clipId"), frame ("start"), frame ("length"))).dump();
        if (cmd == "splitClip") return result (s.splitClip (str ("clipId"), frame ("at"))).dump();
        if (cmd == "deleteClip") return result (s.deleteClip (str ("clipId"))).dump();
        if (cmd == "duplicateClip") return result (s.duplicateClip (str ("clipId"))).dump();
        if (cmd == "setFades") return result (s.setClipFades (str ("clipId"), frame ("fadeIn"), frame ("fadeOut"))).dump();
        if (cmd == "setClipGain") return result (s.setClipGain (str ("clipId"), (float) num ("db"))).dump();
        if (cmd == "undo") return result (s.undo()).dump();
        if (cmd == "redo") return result (s.redo()).dump();
        if (cmd == "endParamGesture") { s.endParamGesture(); return result ({}).dump(); }
        if (cmd == "setTempo") return result (s.setTempo (num ("bpm", 120), (int) num ("beatsPerBar", 4))).dump();
        if (cmd == "setMetronome") return result (s.setMetronome (flag ("on"))).dump();
        if (cmd == "setLatencyOffset") return result (s.setRecordLatencyOffset (frame ("frames"))).dump();
        if (cmd == "play") { if (! s.isOpen()) return result (OpResult::fail ("no hay proyecto abierto")).dump(); e.play(); return json { { "ok", true } }.dump(); }
        if (cmd == "stop")
        {
            if (s.isRecording()) return result (s.stopRecording()).dump();
            e.stop();
            return json { { "ok", true } }.dump();
        }
        if (cmd == "seek") { e.seek (frame ("frame")); return json { { "ok", true } }.dump(); }
        if (cmd == "setMonitoring") { e.setMonitoring (flag ("on"), (float) num ("gain", 1.0)); return json { { "ok", true } }.dump(); }
        if (cmd == "setRecordDuck") { e.setRecordDuck ((float) num ("db", 0.0)); return json { { "ok", true } }.dump(); }
        if (cmd == "startRecording") return result (s.startRecording (str ("trackId"), (int) num ("channels", 1), true, (int) num ("countInBars", 0))).dump();
        if (cmd == "stopRecording") return result (s.stopRecording()).dump();
        if (cmd == "exportDaw")
        {
            DawExportOptions o;
            o.wet = q.value ("mode", std::string ("wet")) != "dry";
            o.bitDepth = (int) num ("bits", 24);
            o.includeMix = q.value ("mix", true);
            o.includeProject = q.value ("project", true);
            json j = result (s.exportDaw (str ("path"), o));
            if (j.value ("ok", false)) { j["jobId"] = j.value ("id", std::string()); j.erase ("id"); }
            return j.dump();
        }
        if (cmd == "exportMidi")
        {
            json j = result (s.exportMidi (str ("path")));
            if (j.value ("ok", false)) { j["notes"] = std::atoi (j.value ("id", std::string ("0")).c_str()); j.erase ("id"); }
            return j.dump();
        }
        if (cmd == "export")
        {
            long clipped = 0;
            json j = result (s.exportMix (str ("path"), (int) num ("bits", 24), frame ("tail"), &clipped));
            j["clippedSamples"] = clipped;
            return j.dump();
        }
        return json { { "ok", false }, { "error", "comando desconocido: " + cmd } }.dump();
    }
    catch (const std::exception& ex)
    {
        return json { { "ok", false }, { "error", std::string ("parámetros no válidos: ") + ex.what() } }.dump();
    }
}
} // namespace cs
