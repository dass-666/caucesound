#include "cs/Archive.h"

#include "cs/Project.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace cs
{
namespace
{
constexpr uint32_t kLocalSig = 0x04034b50, kCentralSig = 0x02014b50, kEndSig = 0x06054b50;
constexpr long long kMaxEntry = 0xFFFFFFFFll;     // sin ZIP64
constexpr long long kMaxTotal = 8ll << 30;        // 8 GB extraídos como máximo
constexpr int kMaxEntries = 4096;
constexpr long long kMaxProjectJson = 64ll << 20; // project.json se lee entero en memoria: 64 MB como máximo
constexpr long long kMaxCentralDir = 16ll << 20;  // el índice del ZIP también: 4096 entradas caben de sobra
constexpr size_t kChunk = 1 << 16;

uint32_t crcTable[256];
bool crcInit = []
{
    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[i] = c;
    }
    return true;
}();

uint32_t crcUpdate (uint32_t crc, const uint8_t* p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

struct File
{
    FILE* f = nullptr;
    File (const std::string& path, const char* mode) : f (std::fopen (path.c_str(), mode)) {}
    ~File() { if (f) std::fclose (f); }
    File (const File&) = delete;
    File& operator= (const File&) = delete;
};

void put16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
void put32 (std::vector<uint8_t>& b, uint32_t v) { put16 (b, v & 0xFFFF); put16 (b, v >> 16); }
uint32_t get16 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }
uint32_t get32 (const uint8_t* p) { return get16 (p) | (get16 (p + 2) << 16); }

// Solo «project.json» y «audio/<nombre>.wav» con caracteres seguros: nada absoluto, ni «..», ni barras invertidas.
bool allowedName (const std::string& n)
{
    if (n == "project.json") return true;
    if (n.size() < 11 || n.rfind ("audio/", 0) != 0) return false;
    const std::string base = n.substr (6);
    if (base.size() < 5 || base.compare (base.size() - 4, 4, ".wav") != 0) return false;
    for (char c : base)
        if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return base.find ("..") == std::string::npos;
}

ArchiveResult fail (std::string e) { ArchiveResult r; r.ok = false; r.error = std::move (e); return r; }
} // namespace

ArchiveResult writeStoredZip (const std::vector<ZipSource>& entries, const std::string& outPath)
{
    if (entries.empty() || entries.size() > 65000) return fail ("número de archivos no admitido");
    const std::string tmp = outPath + ".part";
    std::vector<uint8_t> central;
    ArchiveResult res;
    {
        File out (tmp, "wb");
        if (! out.f) return fail ("no se pudo crear el archivo de salida");
        long long offset = 0;
        std::vector<uint8_t> buf (kChunk);
        for (const auto& en : entries)
        {
            const std::string& name = en.name;
            const std::string& path = en.path;
            // Primera pasada: tamaño y CRC.
            uint32_t crc = 0;
            long long size = 0;
            {
                File in (path, "rb");
                if (! in.f) { std::remove (tmp.c_str()); return fail ("falta un archivo: " + name); }
                size_t n;
                while ((n = std::fread (buf.data(), 1, buf.size(), in.f)) > 0) { crc = crcUpdate (crc, buf.data(), n); size += (long long) n; }
            }
            if (size > kMaxEntry || offset > kMaxEntry) { std::remove (tmp.c_str()); return fail ("el archivo supera 4 GB: no cabe en un ZIP sin ZIP64"); }
            std::vector<uint8_t> h;
            put32 (h, kLocalSig); put16 (h, 20); put16 (h, 0x0800); put16 (h, 0); put16 (h, 0); put16 (h, 0x21);
            put32 (h, crc); put32 (h, (uint32_t) size); put32 (h, (uint32_t) size); put16 (h, (uint32_t) name.size()); put16 (h, 0);
            h.insert (h.end(), name.begin(), name.end());
            if (std::fwrite (h.data(), 1, h.size(), out.f) != h.size()) { std::remove (tmp.c_str()); return fail ("error de escritura (¿espacio?)"); }
            // Segunda pasada: datos (comprobando que el archivo no cambió entre medias).
            uint32_t crc2 = 0;
            long long size2 = 0;
            {
                File in (path, "rb");
                if (! in.f) { std::remove (tmp.c_str()); return fail ("falta un archivo: " + name); }
                size_t n;
                while ((n = std::fread (buf.data(), 1, buf.size(), in.f)) > 0)
                {
                    crc2 = crcUpdate (crc2, buf.data(), n); size2 += (long long) n;
                    if (std::fwrite (buf.data(), 1, n, out.f) != n) { std::remove (tmp.c_str()); return fail ("error de escritura (¿espacio?)"); }
                }
            }
            if (crc2 != crc || size2 != size) { std::remove (tmp.c_str()); return fail ("un archivo cambió durante la exportación: " + name); }
            put32 (central, kCentralSig); put16 (central, 20); put16 (central, 20); put16 (central, 0x0800); put16 (central, 0);
            put16 (central, 0); put16 (central, 0x21); put32 (central, crc); put32 (central, (uint32_t) size); put32 (central, (uint32_t) size);
            put16 (central, (uint32_t) name.size()); put16 (central, 0); put16 (central, 0); put16 (central, 0); put16 (central, 0);
            put32 (central, 0); put32 (central, (uint32_t) offset);
            central.insert (central.end(), name.begin(), name.end());
            offset += (long long) h.size() + size;
            res.bytes += size;
            ++res.files;
        }
        if (offset > kMaxEntry) { std::remove (tmp.c_str()); return fail ("el archivo supera 4 GB: no cabe en un ZIP sin ZIP64"); }
        std::vector<uint8_t> end;
        put32 (end, kEndSig); put16 (end, 0); put16 (end, 0); put16 (end, (uint32_t) entries.size()); put16 (end, (uint32_t) entries.size());
        put32 (end, (uint32_t) central.size()); put32 (end, (uint32_t) offset); put16 (end, 0);
        if (std::fwrite (central.data(), 1, central.size(), out.f) != central.size() || std::fwrite (end.data(), 1, end.size(), out.f) != end.size()
            || std::fflush (out.f) != 0)
        { std::remove (tmp.c_str()); return fail ("error de escritura (¿espacio?)"); }
    }
    std::error_code ec;
    fs::rename (tmp, outPath, ec);
    if (ec) { std::remove (tmp.c_str()); return fail ("no se pudo completar el archivo"); }
    return res;
}

bool listStoredZip (const std::string& path, std::vector<ZipInfo>& out, std::string& error)
{
    out.clear();
    File in (path, "rb");
    if (! in.f) { error = "no se pudo abrir el archivo"; return false; }
    if (std::fseek (in.f, 0, SEEK_END) != 0) { error = "archivo no válido"; return false; }
    const long long fileSize = std::ftell (in.f);
    if (fileSize < 22) { error = "archivo demasiado corto"; return false; }
    const long long tail = std::min<long long> (fileSize, 65535 + 22);
    std::vector<uint8_t> t ((size_t) tail);
    std::fseek (in.f, (long) (fileSize - tail), SEEK_SET);
    if (std::fread (t.data(), 1, t.size(), in.f) != t.size()) { error = "archivo no válido"; return false; }
    long long eocd = -1;
    for (long long i = tail - 22; i >= 0; --i) if (get32 (&t[(size_t) i]) == kEndSig) { eocd = i; break; }
    if (eocd < 0) { error = "no es un ZIP válido"; return false; }
    const uint8_t* e = &t[(size_t) eocd];
    const uint32_t count = get16 (e + 10), cdSize = get32 (e + 12), cdOffset = get32 (e + 16);
    if (cdSize > kMaxCentralDir || (long long) cdOffset + cdSize > fileSize) { error = "ZIP dañado"; return false; }
    std::vector<uint8_t> cd (cdSize);
    std::fseek (in.f, (long) cdOffset, SEEK_SET);
    if (std::fread (cd.data(), 1, cd.size(), in.f) != cd.size()) { error = "ZIP dañado"; return false; }
    size_t p = 0;
    std::vector<uint8_t> buf (kChunk);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (p + 46 > cd.size() || get32 (&cd[p]) != kCentralSig) { error = "ZIP dañado (directorio central)"; return false; }
        const uint32_t method = get16 (&cd[p + 10]), crc = get32 (&cd[p + 16]), csize = get32 (&cd[p + 20]), usize = get32 (&cd[p + 24]);
        const uint32_t nlen = get16 (&cd[p + 28]), xlen = get16 (&cd[p + 30]), clen = get16 (&cd[p + 32]), off = get32 (&cd[p + 42]);
        if (p + 46 + nlen + xlen + clen > cd.size()) { error = "ZIP dañado (nombres)"; return false; }
        ZipInfo info;
        info.name.assign ((const char*) &cd[p + 46], nlen);
        info.size = usize;
        p += 46 + nlen + xlen + clen;
        if (method != 0 || csize != usize) { error = "entrada comprimida: " + info.name; return false; }
        // Datos: tras la cabecera local. Se comprueba el CRC de cada entrada.
        uint8_t lh[30];
        if (std::fseek (in.f, (long) off, SEEK_SET) != 0 || std::fread (lh, 1, 30, in.f) != 30 || get32 (lh) != kLocalSig) { error = "ZIP dañado (cabecera local)"; return false; }
        info.dataOffset = (long long) off + 30 + get16 (lh + 26) + get16 (lh + 28);
        if (info.dataOffset + (long long) usize > fileSize) { error = "ZIP truncado: " + info.name; return false; }
        std::fseek (in.f, (long) info.dataOffset, SEEK_SET);
        uint32_t c = 0;
        long long left = usize;
        while (left > 0)
        {
            const size_t n = std::fread (buf.data(), 1, (size_t) std::min<long long> (left, (long long) buf.size()), in.f);
            if (n == 0) { error = "ZIP truncado: " + info.name; return false; }
            c = crcUpdate (c, buf.data(), n);
            left -= (long long) n;
        }
        if (c != crc) { error = "CRC incorrecto: " + info.name; return false; }
        out.push_back (info);
    }
    return true;
}

bool readStoredZipEntry (const std::string& path, const ZipInfo& info, std::string& bytes)
{
    File in (path, "rb");
    if (! in.f || std::fseek (in.f, (long) info.dataOffset, SEEK_SET) != 0) return false;
    bytes.resize ((size_t) info.size);
    return info.size == 0 || std::fread (&bytes[0], 1, bytes.size(), in.f) == bytes.size();
}

ArchiveResult exportProjectArchive (const std::string& folder, const std::string& outPath)
{
    std::string text, err;
    if (! readFile (folder + "/project.json", text)) return fail ("no se encontró project.json");
    ProjectModel model;
    if (! fromJson (text, model, err)) return fail ("proyecto dañado: " + err);

    // Lista de archivos: project.json + fuentes referenciadas (sin duplicados).
    std::vector<std::string> names { "project.json" };
    std::set<std::string> seen;
    for (const auto& s : model.sources)
    {
        if (! allowedName (s.file)) return fail ("ruta de audio no admitida en el proyecto: " + s.file);
        if (seen.insert (s.file).second) names.push_back (s.file);
    }

    std::vector<ZipSource> entries;
    for (const auto& n : names) entries.push_back ({ n, folder + "/" + n });
    ArchiveResult res = writeStoredZip (entries, outPath);
    if (! res.ok) return res;
    res.name = model.name;
    return res;
}

ArchiveResult importProjectArchive (const std::string& archivePath, const std::string& destFolder)
{
    std::error_code ec;
    if (fs::exists (destFolder, ec)) return fail ("la carpeta de destino ya existe");
    File in (archivePath, "rb");
    if (! in.f) return fail ("no se pudo abrir el archivo");
    if (std::fseek (in.f, 0, SEEK_END) != 0) return fail ("archivo no válido");
    const long long fileSize = std::ftell (in.f);
    if (fileSize < 22) return fail ("no es un proyecto de CAUCESOUND (archivo demasiado corto)");

    // Fin del directorio central: en los últimos 64 KB + 22 bytes.
    const long long tail = std::min<long long> (fileSize, 65535 + 22);
    std::vector<uint8_t> t ((size_t) tail);
    std::fseek (in.f, (long) (fileSize - tail), SEEK_SET);
    if (std::fread (t.data(), 1, t.size(), in.f) != t.size()) return fail ("archivo no válido");
    long long eocd = -1;
    for (long long i = tail - 22; i >= 0; --i) if (get32 (&t[(size_t) i]) == kEndSig) { eocd = i; break; }
    if (eocd < 0) return fail ("no es un ZIP válido");
    const uint8_t* e = &t[(size_t) eocd];
    const uint32_t entries = get16 (e + 10), cdSize = get32 (e + 12), cdOffset = get32 (e + 16);
    if (entries == 0 || entries > (uint32_t) kMaxEntries || cdSize > kMaxCentralDir || (long long) cdOffset + cdSize > fileSize) return fail ("ZIP dañado o no admitido");

    struct Entry { std::string name; uint32_t crc, size, offset; };
    std::vector<Entry> list;
    std::vector<uint8_t> cd (cdSize);
    std::fseek (in.f, (long) cdOffset, SEEK_SET);
    if (std::fread (cd.data(), 1, cd.size(), in.f) != cd.size()) return fail ("ZIP dañado");
    size_t p = 0;
    long long total = 0;
    std::set<std::string> names;
    for (uint32_t i = 0; i < entries; ++i)
    {
        if (p + 46 > cd.size() || get32 (&cd[p]) != kCentralSig) return fail ("ZIP dañado (directorio central)");
        const uint32_t flags = get16 (&cd[p + 8]), method = get16 (&cd[p + 10]), crc = get32 (&cd[p + 16]);
        const uint32_t csize = get32 (&cd[p + 20]), usize = get32 (&cd[p + 24]);
        const uint32_t nlen = get16 (&cd[p + 28]), xlen = get16 (&cd[p + 30]), clen = get16 (&cd[p + 32]), off = get32 (&cd[p + 42]);
        if (p + 46 + nlen + xlen + clen > cd.size()) return fail ("ZIP dañado (nombres)");
        std::string name ((const char*) &cd[p + 46], nlen);
        p += 46 + nlen + xlen + clen;
        if (! name.empty() && name.back() == '/') continue; // carpetas: se ignoran
        if (method != 0 || csize != usize)
            return fail ("el archivo está comprimido (" + name + "): solo se admiten los .caucesound exportados por la app");
        if (flags & 0x1) return fail ("ZIP cifrado: no admitido");
        if (! allowedName (name)) return fail ("contenido no permitido en el archivo: " + name);
        if (name == "project.json" && usize > kMaxProjectJson) return fail ("project.json demasiado grande");
        if (! names.insert (name).second) return fail ("archivo duplicado en el ZIP: " + name);
        total += usize;
        if (total > kMaxTotal) return fail ("el proyecto es demasiado grande");
        list.push_back ({ name, crc, usize, off });
    }
    if (! names.count ("project.json")) return fail ("no es un proyecto de CAUCESOUND (falta project.json)");

    // Extracción a una carpeta temporal; se valida y se renombra al final.
    const std::string tmp = destFolder + ".importing";
    fs::remove_all (tmp, ec);
    fs::create_directories (tmp + "/audio", ec);
    if (ec) return fail ("no se pudo crear la carpeta del proyecto");
    auto abort = [&] (std::string msg) { std::error_code e2; fs::remove_all (tmp, e2); return fail (std::move (msg)); };
    ArchiveResult res;
    std::vector<uint8_t> buf (kChunk);
    for (const auto& en : list)
    {
        uint8_t lh[30];
        if (std::fseek (in.f, (long) en.offset, SEEK_SET) != 0 || std::fread (lh, 1, 30, in.f) != 30 || get32 (lh) != kLocalSig)
            return abort ("ZIP dañado (cabecera local de " + en.name + ")");
        if (get16 (lh + 8) != 0) return abort ("el archivo está comprimido: " + en.name);
        const long long data = (long long) en.offset + 30 + get16 (lh + 26) + get16 (lh + 28);
        if (data + en.size > fileSize) return abort ("ZIP truncado: " + en.name);
        std::fseek (in.f, (long) data, SEEK_SET);
        File out (tmp + "/" + en.name, "wb");
        if (! out.f) return abort ("no se pudo escribir " + en.name);
        uint32_t crc = 0;
        long long left = en.size;
        while (left > 0)
        {
            const size_t n = (size_t) std::min<long long> (left, (long long) buf.size());
            if (std::fread (buf.data(), 1, n, in.f) != n) return abort ("ZIP truncado: " + en.name);
            crc = crcUpdate (crc, buf.data(), n);
            if (std::fwrite (buf.data(), 1, n, out.f) != n) return abort ("error de escritura (¿espacio?)");
            left -= (long long) n;
        }
        if (crc != en.crc) return abort ("archivo dañado (CRC incorrecto): " + en.name);
        res.bytes += en.size;
        ++res.files;
    }

    // El proyecto se valida igual que al abrirlo; todas sus fuentes deben venir en el archivo. Id nuevo.
    std::string text, err;
    ProjectModel model;
    if (! readFile (tmp + "/project.json", text) || ! fromJson (text, model, err)) return abort ("proyecto no válido: " + err);
    for (const auto& s : model.sources)
        if (! names.count (s.file)) return abort ("falta el audio " + s.file + " en el archivo");
    auto j = nlohmann::json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) return abort ("proyecto no válido");
    j["id"] = newId ("prj");
    if (! atomicWriteFile (tmp + "/project.json", j.dump (2), err, false)) return abort (err);
    std::remove ((tmp + "/project.json.bak").c_str());
    fs::rename (tmp, destFolder, ec);
    if (ec) return abort ("no se pudo completar la importación");
    res.name = model.name;
    return res;
}
} // namespace cs
