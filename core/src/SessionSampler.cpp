// Session: Cauce Sampler e instrumentos de paquetes (misión 0.9). Ver cs/Packs.h y docs/CAUCE_SAMPLER.md.
#include "cs/Session.h"

#include "cs/Instruments.h"
#include "cs/Packs.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace cs
{
using nlohmann::json;

OpResult Session::setSamplerInstrument (const std::string& trackId, const std::string& key)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    TrackModel* t = proj.findTrack (trackId);
    if (! t || t->kind != "synth") return OpResult::fail ("solo las pistas de instrumento melódico admiten instrumentos de muestras");
    InstalledPack pack;
    PackInstrument info;
    if (! packLibrary().findInstrument (key, pack, info)) return OpResult::fail ("instrumento no instalado: " + key);
    if (info.kind != "melodic") return OpResult::fail ("ese instrumento es un kit de batería: úselo en una pista de batería");
    // Se prepara la instancia ANTES de tocar el proyecto: si el mapa o las muestras fallan, nada cambia.
    auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.sampler"));
    if (! inst) return OpResult::fail ("Cauce Sampler no disponible");
    json st = { { "instrument", key }, { "packVersion", pack.manifest.version } };
    if (t->instrument.type == "cs.sampler")
    {
        // Cambiar de instrumento conserva los ajustes del sampler (volumen, release...) del instrumento anterior.
        const json old = json::parse (t->instrument.state, nullptr, false);
        if (old.is_object()) for (auto it = old.begin(); it != old.end(); ++it) if (it.value().is_number()) st[it.key()] = it.value();
    }
    if (! inst->loadState (st.dump())) return OpResult::fail ("estado no válido");
    inst->prepare (proj.sampleRate, 1024);
    const SamplerStatus status = samplerStatus (inst.get());
    if (! status.loaded) return OpResult::fail ("no se pudo cargar el instrumento: " + status.error);
    pushUndo();
    t->instrument.type = "cs.sampler";
    t->instrument.state = inst->saveState();
    t->instrument.preset = key;
    instrumentInstances[t->id] = inst;
    return finishEditLocked();
}

OpResult Session::setDrumSampleKit (const std::string& trackId, const std::string& key)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    TrackModel* t = proj.findTrack (trackId);
    if (! t || t->kind != "drums" || t->instrument.type != "cs.drums") return OpResult::fail ("no es una pista de batería");
    json st = json::parse (t->instrument.state.empty() ? std::string ("{}") : t->instrument.state, nullptr, false);
    if (! st.is_object()) st = json::object();
    std::string version;
    if (! key.empty())
    {
        InstalledPack pack;
        PackInstrument info;
        if (! packLibrary().findInstrument (key, pack, info)) return OpResult::fail ("kit no instalado: " + key);
        if (info.kind != "drumkit") return OpResult::fail ("ese instrumento no es un kit de batería");
        version = pack.manifest.version;
        st["sampleKit"] = key;
        st["sampleKitVersion"] = version;
    }
    else
    {
        st.erase ("sampleKit");
        st.erase ("sampleKitVersion");
    }
    auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.drums"));
    if (! inst || ! inst->loadState (st.dump())) return OpResult::fail ("estado no válido");
    inst->prepare (proj.sampleRate, 1024);
    if (! key.empty())
    {
        const SamplerStatus status = samplerStatus (inst.get());
        if (! status.loaded) return OpResult::fail ("no se pudo cargar el kit: " + status.error);
    }
    pushUndo();
    t->instrument.state = inst->saveState();
    instrumentInstances[t->id] = inst;
    return finishEditLocked();
}

OpResult Session::setPreviewInstrument (const std::string& key)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (key.empty())
    {
        previewInstrument.reset();
        previewKey.clear();
        if (open) rebuildGraph();
        return {};
    }
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if ((int) proj.tracks.size() >= kMaxTracks) return OpResult::fail ("no queda una ranura libre para escuchar");
    if (key != previewKey || ! previewInstrument)
    {
        InstalledPack pack;
        PackInstrument info;
        if (! packLibrary().findInstrument (key, pack, info)) return OpResult::fail ("instrumento no instalado: " + key);
        std::shared_ptr<Instrument> inst;
        if (info.kind == "drumkit")
        {
            inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.drums"));
            if (inst) inst->loadState (json { { "sampleKit", key } }.dump());
        }
        else
        {
            inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.sampler"));
            if (inst) inst->loadState (json { { "instrument", key } }.dump());
        }
        if (! inst) return OpResult::fail ("Cauce Sampler no disponible");
        inst->prepare (proj.sampleRate, 1024);
        const SamplerStatus status = samplerStatus (inst.get());
        if (! status.loaded) return OpResult::fail ("no se pudo cargar: " + status.error);
        previewInstrument = inst;
        previewKey = key;
    }
    rebuildGraph();
    OpResult r;
    r.id = std::to_string (proj.tracks.size());
    return r;
}

int Session::previewSlot() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    return previewInstrument && (int) proj.tracks.size() < kMaxTracks ? (int) proj.tracks.size() : -1;
}

std::vector<std::string> Session::missingInstruments() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    std::vector<std::string> out;
    for (const auto& t : proj.tracks)
    {
        std::string key;
        const json st = json::parse (t.instrument.state.empty() ? std::string ("{}") : t.instrument.state, nullptr, false);
        if (! st.is_object()) continue;
        if (t.instrument.type == "cs.sampler") key = st.value ("instrument", std::string());
        else if (t.instrument.type == "cs.drums") key = st.value ("sampleKit", std::string());
        if (key.empty()) continue;
        auto it = instrumentInstances.find (t.id);
        const bool loaded = it != instrumentInstances.end() && it->second && samplerStatus (it->second.get()).loaded;
        if (! loaded && std::find (out.begin(), out.end(), key) == out.end()) out.push_back (key);
    }
    return out;
}
std::vector<std::string> Session::usedPackInstruments() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    std::vector<std::string> out;
    if (! open) return out;
    for (const auto& t : proj.tracks)
    {
        const json st = json::parse (t.instrument.state.empty() ? std::string ("{}") : t.instrument.state, nullptr, false);
        if (! st.is_object()) continue;
        std::string key;
        if (t.instrument.type == "cs.sampler") key = st.value ("instrument", std::string());
        else if (t.instrument.type == "cs.drums") key = st.value ("sampleKit", std::string());
        if (! key.empty() && std::find (out.begin(), out.end(), key) == out.end()) out.push_back (key);
    }
    return out;
}
} // namespace cs
