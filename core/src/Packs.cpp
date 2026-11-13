// Cauce Packs: manifiesto, biblioteca instalada e importación segura de ZIP. Ver cs/Packs.h y docs/CAUCE_PACK_FORMAT.md.
#include "cs/Packs.h"
#include "cs/Hash.h"
#include "cs/Project.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <set>

#if CS_HAVE_ZLIB
#include <zlib.h>
#endif

namespace fs = std::filesystem;

namespace cs
{
using nlohmann::json;

const PackInstrument* PackManifest::findInstrument (const std::string& iid) const
{
    for (const auto& i : instruments) if (i.id == iid) return &i;
    return nullptr;
}

namespace
{
bool validId (const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
    return s.front() != '.' && s.front() != '-';
}

bool validVersion (const std::string& v)
{
    int parts = 0;
    size_t i = 0;
    while (i < v.size())
    {
        size_t j = i;
        while (j < v.size() && std::isdigit ((unsigned char) v[j])) ++j;
        if (j == i || j - i > 6) return false;
        ++parts;
        if (j == v.size()) break;
        if (v[j] != '.') return false;
        i = j + 1;
        if (i == v.size()) return false;
    }
    return parts == 3;
}

// Ruta interna segura: relativa, con '/', sin «..», sin «.» inicial en ningún tramo, sin caracteres de control.
bool safePackPath (const std::string& p)
{
    if (p.empty() || p.size() > 240 || p[0] == '/' || p.find ('\\') != std::string::npos || p.find (':') != std::string::npos) return false;
    for (char c : p) if ((unsigned char) c < 32) return false;
    size_t s = 0;
    while (s <= p.size())
    {
        size_t e = p.find ('/', s);
        if (e == std::string::npos) e = p.size();
        const std::string part = p.substr (s, e - s);
        if (part.empty() || part == "." || part == ".." || part[0] == '.') return false;
        s = e + 1;
    }
    return true;
}

bool allowedExtension (const std::string& p)
{
    static const char* ok[] = { ".wav", ".sfz", ".json", ".txt", ".md" };
    std::string l = p;
    for (char& c : l) c = (char) std::tolower ((unsigned char) c);
    for (const char* e : ok)
    {
        const size_t n = std::strlen (e);
        if (l.size() > n && l.compare (l.size() - n, n, e) == 0) return true;
    }
    return false;
}

std::vector<std::string> stringList (const json& j, const char* key)
{
    std::vector<std::string> out;
    if (j.contains (key) && j[key].is_array())
        for (const auto& v : j[key]) if (v.is_string()) out.push_back (v.get<std::string>());
    return out;
}
} // namespace

int compareVersions (const std::string& a, const std::string& b)
{
    auto parts = [] (const std::string& v)
    {
        std::vector<long> p;
        size_t i = 0;
        while (i <= v.size())
        {
            size_t j = v.find ('.', i);
            if (j == std::string::npos) j = v.size();
            p.push_back (std::strtol (v.substr (i, j - i).c_str(), nullptr, 10));
            i = j + 1;
        }
        while (p.size() < 3) p.push_back (0);
        return p;
    };
    const auto pa = parts (a), pb = parts (b);
    for (size_t i = 0; i < 3; ++i) { if (pa[i] < pb[i]) return -1; if (pa[i] > pb[i]) return 1; }
    return 0;
}

bool parsePackManifest (const std::string& text, PackManifest& out, std::string& error)
{
    const json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) { error = "manifiesto: JSON no válido"; return false; }
    try
    {
        if (j.value ("format", std::string()) != "cauce-pack") { error = "no es un paquete de CAUCESOUND"; return false; }
        PackManifest m;
        m.formatVersion = j.at ("formatVersion").get<int>();
        if (m.formatVersion > kPackFormatVersion) { error = "paquete creado para una versión más nueva de la app"; return false; }
        if (m.formatVersion < 1) { error = "formatVersion no válido"; return false; }
        m.id = j.at ("id").get<std::string>();
        m.version = j.at ("version").get<std::string>();
        m.name = j.at ("name").get<std::string>();
        m.author = j.value ("author", std::string());
        m.description = j.value ("description", std::string());
        m.license = j.at ("license").get<std::string>();
        m.licenseFile = j.at ("licenseFile").get<std::string>();
        m.minEngine = j.value ("minEngine", std::string ("0.9.0"));
        if (! validId (m.id)) { error = "id de paquete no válido: " + m.id; return false; }
        if (! validVersion (m.version)) { error = "versión no válida (x.y.z): " + m.version; return false; }
        if (m.name.empty() || m.license.empty()) { error = "faltan nombre o licencia"; return false; }
        if (! safePackPath (m.licenseFile)) { error = "ruta de licencia no válida"; return false; }
        if (j.contains ("sources") && j["sources"].is_array())
            for (const auto& s : j["sources"])
                m.sources.push_back ({ s.value ("name", std::string()), s.value ("author", std::string()), s.value ("url", std::string()),
                                       s.value ("version", std::string()), s.value ("license", std::string()), s.value ("retrieved", std::string()),
                                       s.value ("notes", std::string()) });
        std::set<std::string> files;
        for (const auto& f : j.at ("files"))
        {
            PackFileEntry e;
            e.path = f.at ("path").get<std::string>();
            e.bytes = f.at ("bytes").get<long long>();
            e.sha256 = f.at ("sha256").get<std::string>();
            if (! safePackPath (e.path) || ! allowedExtension (e.path)) { error = "ruta no permitida en el paquete: " + e.path; return false; }
            if (e.bytes < 0 || e.sha256.size() != 64) { error = "entrada de archivo no válida: " + e.path; return false; }
            if (! files.insert (e.path).second) { error = "archivo repetido: " + e.path; return false; }
            m.files.push_back (e);
        }
        if (! files.count (m.licenseFile)) { error = "el archivo de licencia no está en la lista de archivos"; return false; }
        std::set<std::string> ids;
        for (const auto& ji : j.at ("instruments"))
        {
            PackInstrument i;
            i.id = ji.at ("id").get<std::string>();
            i.name = ji.at ("name").get<std::string>();
            i.family = ji.at ("family").get<std::string>();
            i.kind = ji.value ("kind", std::string ("melodic"));
            i.sfz = ji.at ("sfz").get<std::string>();
            i.description = ji.value ("description", std::string());
            i.origin = ji.value ("origin", std::string());
            i.tags = stringList (ji, "tags");
            i.genres = stringList (ji, "genres");
            i.laneNames = stringList (ji, "lanes");
            i.polyphony = std::clamp (ji.value ("polyphony", 32), 1, kSamplerMaxVoices / 2);
            i.mono = ji.value ("mono", false);
            i.glideSeconds = std::clamp (ji.value ("glide", 0.0f), 0.0f, 2.0f);
            i.volumeDb = std::clamp (ji.value ("volumeDb", 0.0f), -40.0f, 12.0f);
            i.previewKey = std::clamp (ji.value ("previewKey", i.kind == "drumkit" ? 36 : 60), 0, 127);
            i.lowKey = std::clamp (ji.value ("lowKey", 0), 0, 127);
            i.highKey = std::clamp (ji.value ("highKey", 127), 0, 127);
            if (! validId (i.id) || ! ids.insert (i.id).second) { error = "id de instrumento no válido o repetido: " + i.id; return false; }
            if (i.kind != "drumkit" && i.kind != "melodic") { error = "tipo de instrumento desconocido: " + i.kind; return false; }
            if (! files.count (i.sfz)) { error = "el mapa " + i.sfz + " no está en la lista de archivos"; return false; }
            m.instruments.push_back (i);
        }
        if (m.instruments.empty()) { error = "el paquete no tiene instrumentos"; return false; }
        out = std::move (m);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string ("manifiesto incompleto: ") + e.what();
        return false;
    }
}

std::string packManifestToJson (const PackManifest& m)
{
    json j;
    j["format"] = "cauce-pack";
    j["formatVersion"] = m.formatVersion;
    j["id"] = m.id; j["version"] = m.version; j["name"] = m.name; j["author"] = m.author; j["description"] = m.description;
    j["license"] = m.license; j["licenseFile"] = m.licenseFile; j["minEngine"] = m.minEngine;
    json src = json::array();
    for (const auto& s : m.sources)
        src.push_back ({ { "name", s.name }, { "author", s.author }, { "url", s.url }, { "version", s.version }, { "license", s.license },
                         { "retrieved", s.retrieved }, { "notes", s.notes } });
    j["sources"] = src;
    json ins = json::array();
    for (const auto& i : m.instruments)
        ins.push_back ({ { "id", i.id }, { "name", i.name }, { "family", i.family }, { "kind", i.kind }, { "sfz", i.sfz }, { "description", i.description },
                         { "origin", i.origin }, { "tags", i.tags }, { "genres", i.genres }, { "lanes", i.laneNames }, { "polyphony", i.polyphony },
                         { "mono", i.mono }, { "glide", i.glideSeconds }, { "volumeDb", i.volumeDb }, { "previewKey", i.previewKey },
                         { "lowKey", i.lowKey }, { "highKey", i.highKey } });
    j["instruments"] = ins;
    json files = json::array();
    for (const auto& f : m.files) files.push_back ({ { "path", f.path }, { "bytes", f.bytes }, { "sha256", f.sha256 } });
    j["files"] = files;
    return j.dump (2);
}

bool fillPackFiles (const std::string& dir, PackManifest& m, std::string& error)
{
    m.files.clear();
    std::error_code ec;
    std::vector<std::string> rels;
    for (auto it = fs::recursive_directory_iterator (dir, ec); ! ec && it != fs::recursive_directory_iterator(); it.increment (ec))
    {
        if (! it->is_regular_file()) continue;
        std::string rel = fs::relative (it->path(), dir, ec).generic_string();
        if (rel == "cauce-pack.json") continue;
        rels.push_back (rel);
    }
    if (ec) { error = "no se pudo recorrer " + dir; return false; }
    std::sort (rels.begin(), rels.end());
    for (const auto& rel : rels)
    {
        if (! safePackPath (rel) || ! allowedExtension (rel)) { error = "archivo no permitido en un paquete: " + rel; return false; }
        long long size = 0;
        const std::string h = sha256File (dir + "/" + rel, &size);
        if (h.empty()) { error = "no se pudo leer " + rel; return false; }
        m.files.push_back ({ rel, h, size });
    }
    return true;
}

// --- Biblioteca -------------------------------------------------------------------------------------------------------
PackLibrary& packLibrary()
{
    static PackLibrary lib;
    return lib;
}

void PackLibrary::configure (const std::string& factoryDir, const std::string& userDir)
{
    {
        std::lock_guard<std::mutex> l (mtx);
        factory = factoryDir;
        user = userDir;
    }
    if (! userDir.empty()) makeDirs (userDir);
    rescan();
}

namespace
{
void scanRoot (const std::string& root, bool isFactory, std::vector<InstalledPack>& out)
{
    if (root.empty()) return;
    for (const auto& name : listDir (root))
    {
        if (name.empty() || name[0] == '.') continue;
        const std::string d = root + "/" + name;
        std::string text, err;
        if (! readFile (d + "/cauce-pack.json", text)) continue;
        InstalledPack p;
        p.dir = d;
        p.factory = isFactory;
        if (! parsePackManifest (text, p.manifest, err)) { p.complete = false; p.problems.push_back (err); p.manifest.id = name; p.manifest.name = name; out.push_back (p); continue; }
        for (const auto& f : p.manifest.files)
        {
            std::error_code ec;
            const auto sz = fs::file_size (d + "/" + f.path, ec);
            if (ec) { p.complete = false; p.problems.push_back ("falta " + f.path); }
            else if ((long long) sz != f.bytes) { p.complete = false; p.problems.push_back ("tamaño distinto: " + f.path); }
        }
        // Si el mismo id está instalado de fábrica y como importado, gana la versión más alta (empate: fábrica).
        auto same = std::find_if (out.begin(), out.end(), [&] (const InstalledPack& x) { return x.manifest.id == p.manifest.id; });
        if (same == out.end()) out.push_back (std::move (p));
        else if (compareVersions (p.manifest.version, same->manifest.version) > 0) *same = std::move (p);
    }
}
} // namespace

void PackLibrary::rescan()
{
    std::vector<InstalledPack> found;
    std::string f, u;
    {
        std::lock_guard<std::mutex> l (mtx);
        f = factory; u = user;
    }
    scanRoot (f, true, found);
    scanRoot (u, false, found);
    std::sort (found.begin(), found.end(), [] (const InstalledPack& a, const InstalledPack& b) { return a.manifest.id < b.manifest.id; });
    std::lock_guard<std::mutex> l (mtx);
    packs = std::move (found);
}

std::vector<InstalledPack> PackLibrary::list() const
{
    std::lock_guard<std::mutex> l (mtx);
    return packs;
}

bool PackLibrary::find (const std::string& packId, InstalledPack& out) const
{
    std::lock_guard<std::mutex> l (mtx);
    for (const auto& p : packs) if (p.manifest.id == packId) { out = p; return true; }
    return false;
}

bool PackLibrary::findInstrument (const std::string& key, InstalledPack& pack, PackInstrument& instrument) const
{
    const size_t colon = key.find (':');
    if (colon == std::string::npos) return false;
    if (! find (key.substr (0, colon), pack)) return false;
    const PackInstrument* i = pack.manifest.findInstrument (key.substr (colon + 1));
    if (! i) return false;
    instrument = *i;
    return true;
}

std::shared_ptr<const SamplerProgram> PackLibrary::loadInstrument (const std::string& key, int sampleRate, std::string& error)
{
    {
        std::lock_guard<std::mutex> l (mtx);
        cache.erase (std::remove_if (cache.begin(), cache.end(), [] (const CacheEntry& e) { return e.prog.expired(); }), cache.end());
        for (const auto& e : cache)
            if (e.key == key && e.rate == sampleRate)
                if (auto p = e.prog.lock()) return p;
    }
    InstalledPack pack;
    PackInstrument inst;
    if (! findInstrument (key, pack, inst)) { error = "instrumento no instalado: " + key; return nullptr; }
    std::string text;
    if (! readFile (pack.dir + "/" + inst.sfz, text)) { error = "falta el mapa " + inst.sfz; return nullptr; }
    auto prog = std::make_shared<SamplerProgram>();
    if (! parseSfz (text, *prog, error)) return nullptr;
    prog->id = key;
    prog->name = inst.name;
    prog->polyphony = inst.polyphony;
    prog->mono = inst.mono;
    prog->glideSeconds = inst.glideSeconds;
    prog->volumeDb = inst.volumeDb;
    // El mapa vive en instruments/: sus rutas de muestras son relativas a esa carpeta (como en cualquier SFZ).
    std::string sfzDir = pack.dir;
    const size_t slash = inst.sfz.rfind ('/');
    if (slash != std::string::npos) sfzDir += "/" + inst.sfz.substr (0, slash);
    // Las rutas «../samples/...» de un SFZ estándar se resuelven contra la raíz del paquete: se normalizan aquí y
    // después se exige que no salgan de él.
    for (auto& r : prog->regions)
    {
        std::string p = r.samplePath;
        std::string base = slash == std::string::npos ? std::string() : inst.sfz.substr (0, slash);
        while (p.rfind ("../", 0) == 0)
        {
            if (base.empty()) { error = "el mapa apunta fuera del paquete: " + r.samplePath; return nullptr; }
            const size_t s = base.rfind ('/');
            base = s == std::string::npos ? std::string() : base.substr (0, s);
            p = p.substr (3);
        }
        r.samplePath = base.empty() ? p : base + "/" + p;
    }
    if (! loadProgramSamples (*prog, pack.dir, sampleRate, budget, error)) return nullptr;
    std::shared_ptr<const SamplerProgram> result = prog;
    std::lock_guard<std::mutex> l (mtx);
    cache.push_back ({ key, sampleRate, result });
    return result;
}

bool PackLibrary::verify (const std::string& packId, std::vector<std::string>& bad, std::string& error) const
{
    InstalledPack p;
    if (! find (packId, p)) { error = "paquete no instalado"; return false; }
    bad.clear();
    for (const auto& f : p.manifest.files)
        if (sha256File (p.dir + "/" + f.path) != f.sha256) bad.push_back (f.path);
    return bad.empty();
}

bool PackLibrary::remove (const std::string& packId, std::string& error)
{
    InstalledPack p;
    if (! find (packId, p)) { error = "paquete no instalado"; return false; }
    if (p.factory) { error = "los paquetes incluidos en la app no se pueden borrar"; return false; }
    std::error_code ec;
    fs::remove_all (p.dir, ec);
    if (ec) { error = "no se pudo borrar: " + ec.message(); return false; }
    rescan();
    return true;
}

// --- Importación de ZIP -----------------------------------------------------------------------------------------------
namespace
{
uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

struct ZipEntry { std::string name; uint32_t method = 0, crc = 0, csize = 0, usize = 0, localOffset = 0, externalAttr = 0; uint16_t flags = 0, madeBy = 0; };

uint32_t crc32Update (uint32_t c, const uint8_t* d, size_t n)
{
    static uint32_t table[256];
    static bool init = false;
    if (! init)
    {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t k = i; for (int b = 0; b < 8; ++b) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; table[i] = k; }
        init = true;
    }
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool readCentralDirectory (std::FILE* f, long long fileSize, std::vector<ZipEntry>& out, int maxEntries, std::string& error)
{
    const long long tail = std::min<long long> (fileSize, 65535 + 22);
    std::vector<uint8_t> t ((size_t) tail);
    if (std::fseek (f, (long) (fileSize - tail), SEEK_SET) != 0 || std::fread (t.data(), 1, t.size(), f) != t.size()) { error = "archivo no válido"; return false; }
    long long eocd = -1;
    for (long long i = tail - 22; i >= 0; --i) if (rd32 (&t[(size_t) i]) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) { error = "no es un ZIP válido"; return false; }
    const uint8_t* e = &t[(size_t) eocd];
    const uint32_t count = rd16 (e + 10), cdSize = rd32 (e + 12), cdOffset = rd32 (e + 16);
    if (count == 0xFFFF || cdOffset == 0xFFFFFFFFu) { error = "ZIP64 no admitido"; return false; }
    if ((int) count > maxEntries) { error = "demasiados archivos en el ZIP"; return false; }
    if ((long long) cdOffset + cdSize > fileSize || cdSize > 16 * 1024 * 1024) { error = "ZIP dañado"; return false; }
    std::vector<uint8_t> cd (cdSize);
    if (std::fseek (f, (long) cdOffset, SEEK_SET) != 0 || std::fread (cd.data(), 1, cd.size(), f) != cd.size()) { error = "ZIP dañado"; return false; }
    size_t p = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (p + 46 > cd.size() || rd32 (&cd[p]) != 0x02014b50u) { error = "ZIP dañado (directorio central)"; return false; }
        ZipEntry z;
        z.madeBy = rd16 (&cd[p + 4]);
        z.flags = rd16 (&cd[p + 8]);
        z.method = rd16 (&cd[p + 10]);
        z.crc = rd32 (&cd[p + 16]); z.csize = rd32 (&cd[p + 20]); z.usize = rd32 (&cd[p + 24]);
        const uint32_t nlen = rd16 (&cd[p + 28]), xlen = rd16 (&cd[p + 30]), clen = rd16 (&cd[p + 32]);
        z.externalAttr = rd32 (&cd[p + 38]);
        z.localOffset = rd32 (&cd[p + 42]);
        if (p + 46 + nlen + xlen + clen > cd.size()) { error = "ZIP dañado (nombres)"; return false; }
        z.name.assign ((const char*) &cd[p + 46], nlen);
        p += 46 + nlen + xlen + clen;
        out.push_back (z);
    }
    return true;
}

bool extractEntry (std::FILE* f, long long fileSize, const ZipEntry& z, const std::string& dest, std::string& error)
{
    uint8_t lh[30];
    if (std::fseek (f, (long) z.localOffset, SEEK_SET) != 0 || std::fread (lh, 1, 30, f) != 30 || rd32 (lh) != 0x04034b50u) { error = "ZIP dañado (cabecera local): " + z.name; return false; }
    const long long dataOff = (long long) z.localOffset + 30 + rd16 (lh + 26) + rd16 (lh + 28);
    if (dataOff + (long long) z.csize > fileSize) { error = "ZIP truncado: " + z.name; return false; }
    std::fseek (f, (long) dataOff, SEEK_SET);
    std::FILE* o = std::fopen (dest.c_str(), "wb");
    if (! o) { error = "no se pudo escribir " + z.name; return false; }
    std::vector<uint8_t> in (65536), outBuf (65536);
    uint32_t crc = 0;
    long long written = 0;
    bool ok = true;
    if (z.method == 0)
    {
        long long left = z.csize;
        while (left > 0 && ok)
        {
            const size_t n = std::fread (in.data(), 1, (size_t) std::min<long long> (left, (long long) in.size()), f);
            if (n == 0) { error = "ZIP truncado: " + z.name; ok = false; break; }
            crc = crc32Update (crc, in.data(), n);
            ok = std::fwrite (in.data(), 1, n, o) == n;
            written += (long long) n;
            left -= (long long) n;
        }
    }
#if CS_HAVE_ZLIB
    else if (z.method == 8)
    {
        z_stream s {};
        if (inflateInit2 (&s, -MAX_WBITS) != Z_OK) { std::fclose (o); error = "zlib"; return false; }
        long long left = z.csize;
        int rc = Z_OK;
        while (ok && rc != Z_STREAM_END)
        {
            if (s.avail_in == 0)
            {
                if (left <= 0) { error = "ZIP truncado: " + z.name; ok = false; break; }
                const size_t n = std::fread (in.data(), 1, (size_t) std::min<long long> (left, (long long) in.size()), f);
                if (n == 0) { error = "ZIP truncado: " + z.name; ok = false; break; }
                left -= (long long) n;
                s.next_in = in.data();
                s.avail_in = (uInt) n;
            }
            s.next_out = outBuf.data();
            s.avail_out = (uInt) outBuf.size();
            rc = inflate (&s, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { error = "datos comprimidos dañados: " + z.name; ok = false; break; }
            const size_t got = outBuf.size() - s.avail_out;
            written += (long long) got;
            // Nunca se escribe más de lo declarado (protección contra bombas ZIP que mienten en el tamaño).
            if (written > (long long) z.usize) { error = "el archivo descomprimido es mayor de lo declarado: " + z.name; ok = false; break; }
            crc = crc32Update (crc, outBuf.data(), got);
            if (got && std::fwrite (outBuf.data(), 1, got, o) != got) ok = false;
        }
        inflateEnd (&s);
    }
#endif
    else { error = "método de compresión no admitido en " + z.name + " (use ZIP sin compresión o deflate)"; ok = false; }
    if (std::fclose (o) != 0) ok = false;
    if (ok && written != (long long) z.usize) { error = "tamaño incorrecto: " + z.name; ok = false; }
    if (ok && crc != z.crc) { error = "CRC incorrecto: " + z.name; ok = false; }
    return ok;
}

std::string randomSuffix()
{
    std::random_device rd;
    char b[17];
    std::snprintf (b, sizeof b, "%08x%08x", rd(), rd());
    return b;
}
} // namespace

PackImportResult PackLibrary::importZip (const std::string& zipPath, bool replace, const PackImportLimits& limits)
{
    PackImportResult res;
    std::string userRoot;
    {
        std::lock_guard<std::mutex> l (mtx);
        userRoot = user;
    }
    if (userRoot.empty()) { res.error = "no hay carpeta de paquetes de usuario"; return res; }
    std::FILE* f = std::fopen (zipPath.c_str(), "rb");
    if (! f) { res.error = "no se pudo abrir el archivo"; return res; }
    struct Closer { std::FILE* f; ~Closer() { std::fclose (f); } } closer { f };
    std::fseek (f, 0, SEEK_END);
    const long long fileSize = std::ftell (f);
    if (fileSize < 22) { res.error = "archivo demasiado corto"; return res; }
    std::vector<ZipEntry> entries;
    if (! readCentralDirectory (f, fileSize, entries, limits.maxEntries, res.error)) return res;

    // 1) Revisión completa ANTES de escribir nada: rutas, tipos, tamaños, proporción de compresión.
    std::string prefix;
    for (const auto& z : entries)
        if (z.name == "cauce-pack.json" || (z.name.size() > 16 && z.name.compare (z.name.size() - 16, 16, "/cauce-pack.json") == 0 && std::count (z.name.begin(), z.name.end(), '/') == 1))
        {
            prefix = z.name.substr (0, z.name.size() - 15);
            break;
        }
    long long total = 0;
    bool hasManifest = false;
    for (const auto& z : entries)
    {
        if (! z.name.empty() && z.name.back() == '/') continue; // carpeta
        if (z.flags & 1) { res.error = "ZIP cifrado: no admitido"; return res; }
        const uint32_t unixMode = z.externalAttr >> 16;
        if ((z.madeBy >> 8) == 3 && (unixMode & 0170000) == 0120000) { res.error = "el ZIP contiene enlaces simbólicos: " + z.name; return res; }
        if (z.name.compare (0, prefix.size(), prefix) != 0) { res.error = "archivo fuera de la carpeta del paquete: " + z.name; return res; }
        const std::string rel = z.name.substr (prefix.size());
        if (rel == "cauce-pack.json") { hasManifest = true; }
        else if (! safePackPath (rel)) { res.error = "ruta peligrosa en el ZIP: " + z.name; return res; }
        else if (! allowedExtension (rel)) { res.error = "tipo de archivo no admitido en un paquete: " + z.name; return res; }
        if ((long long) z.usize > limits.maxFileBytes) { res.error = "archivo demasiado grande: " + z.name; return res; }
        if (z.csize > 0 && (double) z.usize / (double) z.csize > limits.maxRatio) { res.error = "proporción de compresión sospechosa: " + z.name; return res; }
        if (z.csize == 0 && z.usize > 0) { res.error = "entrada dañada: " + z.name; return res; }
        total += z.usize;
        if (total > limits.maxTotalBytes) { res.error = "el paquete descomprimido supera el límite"; return res; }
    }
    if (! hasManifest) { res.error = "el ZIP no contiene cauce-pack.json"; return res; }

    // 2) Extracción a una carpeta temporal oculta dentro de la de usuario (mismo sistema de archivos: renombrado atómico).
    const std::string tmp = userRoot + "/.import-" + randomSuffix();
    std::error_code ec;
    fs::create_directories (tmp, ec);
    if (ec) { res.error = "no se pudo crear la carpeta temporal"; return res; }
    auto cleanup = [&] { std::error_code e2; fs::remove_all (tmp, e2); };
    for (const auto& z : entries)
    {
        if (! z.name.empty() && z.name.back() == '/') continue;
        const std::string rel = z.name.substr (prefix.size());
        const std::string dest = tmp + "/" + rel;
        fs::create_directories (fs::path (dest).parent_path(), ec);
        if (! extractEntry (f, fileSize, z, dest, res.error)) { cleanup(); return res; }
        ++res.files;
        res.bytes += z.usize;
    }

    // 3) Validación del contenido: manifiesto, lista exacta de archivos, hashes, mapas SFZ legibles.
    std::string text;
    PackManifest m;
    if (! readFile (tmp + "/cauce-pack.json", text) || ! parsePackManifest (text, m, res.error)) { cleanup(); return res; }
    std::set<std::string> listed;
    for (const auto& fe : m.files) listed.insert (fe.path);
    for (const auto& z : entries)
    {
        if (! z.name.empty() && z.name.back() == '/') continue;
        const std::string rel = z.name.substr (prefix.size());
        if (rel != "cauce-pack.json" && ! listed.count (rel)) { res.error = "archivo no declarado en el manifiesto: " + rel; cleanup(); return res; }
    }
    for (const auto& fe : m.files)
    {
        long long size = 0;
        if (sha256File (tmp + "/" + fe.path, &size) != fe.sha256 || size != fe.bytes) { res.error = "el archivo no coincide con su SHA-256: " + fe.path; cleanup(); return res; }
    }
    for (const auto& ins : m.instruments)
    {
        std::string sfz;
        SamplerProgram probe;
        if (! readFile (tmp + "/" + ins.sfz, sfz) || ! parseSfz (sfz, probe, res.error)) { res.error = "mapa no válido (" + ins.sfz + "): " + res.error; cleanup(); return res; }
    }
    res.packId = m.id;
    res.version = m.version;

    // 4) Instalación: no se pisa otra versión sin permiso; nunca se toca un paquete de fábrica.
    InstalledPack existing;
    const bool exists = find (m.id, existing);
    const std::string finalDir = userRoot + "/" + m.id;
    if (exists)
    {
        if (! replace) { res.error = "ya está instalado " + m.id + " " + existing.manifest.version; cleanup(); return res; }
        if (compareVersions (m.version, existing.manifest.version) < 0) { res.error = "la versión instalada es más nueva"; cleanup(); return res; }
        if (existing.factory && compareVersions (m.version, existing.manifest.version) == 0) { res.error = "esa versión ya viene incluida en la app"; cleanup(); return res; }
    }
    std::string backup;
    if (fs::exists (finalDir, ec))
    {
        backup = userRoot + "/.old-" + randomSuffix();
        fs::rename (finalDir, backup, ec);
        if (ec) { res.error = "no se pudo sustituir el paquete anterior"; cleanup(); return res; }
        res.replaced = true;
    }
    fs::rename (tmp, finalDir, ec);
    if (ec)
    {
        if (! backup.empty()) { std::error_code e2; fs::rename (backup, finalDir, e2); }
        res.error = "no se pudo instalar el paquete";
        cleanup();
        return res;
    }
    if (! backup.empty()) fs::remove_all (backup, ec);
    rescan();
    res.ok = true;
    return res;
}
} // namespace cs
