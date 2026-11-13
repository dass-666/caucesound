#pragma once
// Cauce Packs (misión 0.9): paquetes de sonidos versionados y portables (Cauce Signature Sounds y futuros).
//
// Un paquete es una carpeta (o un ZIP con esa carpeta) con:
//   cauce-pack.json   manifiesto: id estable, versión, licencia, procedencia, instrumentos, archivos con SHA-256
//   instruments/*.sfz mapas de muestras en SFZ (formato abierto: otros reproductores, p. ej. un DAW web, pueden leerlos)
//   samples/**.wav    muestras PCM
//   LICENSE.txt       texto de la licencia (obligatorio)
// Especificación completa: docs/CAUCE_PACK_FORMAT.md. No depende de Android: lo usan el motor, las pruebas y `cauce`.
//
// Un instrumento se identifica con "<id del paquete>:<id del instrumento>" (p. ej. "cauce.drums:raw-boombap"). Los
// proyectos guardan ese identificador y la versión del paquete, nunca rutas: si el paquete falta, el proyecto se
// conserva intacto y la pista queda muda y señalada (no se sustituye por otro sonido).
#include "cs/Sampler.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cs
{
constexpr int kPackFormatVersion = 1;

struct PackFileEntry
{
    std::string path, sha256;
    long long bytes = 0;
};

struct PackSource // procedencia de un contenido de terceros (o "original")
{
    std::string name, author, url, version, license, retrieved, notes;
};

struct PackInstrument
{
    std::string id, name, family; // family: drums | keys | strings | bass | melodic | percussion
    std::string kind;             // "drumkit" (notas 36-43 = carriles del Channel Rack) | "melodic"
    std::string sfz;              // ruta relativa del mapa
    std::string description, origin; // origin: "original" | "cc0" | ... (resumen; el detalle está en sources)
    std::vector<std::string> tags, genres, laneNames;
    int polyphony = 32;
    bool mono = false;
    float glideSeconds = 0.0f, volumeDb = 0.0f;
    int previewKey = 60;
    int lowKey = 0, highKey = 127; // rango útil (informativo para la interfaz)
};

struct PackManifest
{
    int formatVersion = kPackFormatVersion;
    std::string id, version, name, author, description, license, licenseFile, minEngine;
    std::vector<PackSource> sources;
    std::vector<PackInstrument> instruments;
    std::vector<PackFileEntry> files;
    long long totalBytes() const { long long t = 0; for (const auto& f : files) t += f.bytes; return t; }
    const PackInstrument* findInstrument (const std::string& id) const;
};

// Valida estructura, ids ([a-z0-9._-]), versión semántica, rutas seguras y que cada SFZ esté listado en files.
bool parsePackManifest (const std::string& json, PackManifest& out, std::string& error);
std::string packManifestToJson (const PackManifest& m);
// Compara versiones "x.y.z": -1, 0 o 1.
int compareVersions (const std::string& a, const std::string& b);

struct InstalledPack
{
    PackManifest manifest;
    std::string dir;
    bool factory = false;              // incluido en la app: no se puede borrar
    bool complete = true;              // todos los archivos del manifiesto existen con su tamaño
    std::vector<std::string> problems; // archivos que faltan o tamaños distintos
};

struct PackImportResult
{
    bool ok = false;
    std::string error, packId, version;
    int files = 0;
    long long bytes = 0;
    bool replaced = false;
};

// Límites de importación (protección contra ZIP maliciosos o desmesurados).
struct PackImportLimits
{
    long long maxTotalBytes = 1024LL * 1024 * 1024; // 1 GB descomprimido
    long long maxFileBytes = 200LL * 1024 * 1024;
    int maxEntries = 5000;
    double maxRatio = 200.0;                         // descomprimido / comprimido por entrada (bombas ZIP)
};

class PackLibrary
{
public:
    // factoryDir: paquetes incluidos (solo lectura); userDir: importados. Cualquiera puede estar vacío.
    void configure (const std::string& factoryDir, const std::string& userDir);
    void rescan();
    std::vector<InstalledPack> list() const;
    bool find (const std::string& packId, InstalledPack& out) const;
    // "<pack>:<instrumento>" -> paquete e instrumento. false si no está instalado.
    bool findInstrument (const std::string& key, InstalledPack& pack, PackInstrument& instrument) const;
    // Carga (o reutiliza) el programa del instrumento a esa frecuencia. Hilo de control (lee disco).
    // Devuelve nullptr con error si falta el paquete o el mapa no es válido. Muestras ausentes: programa con
    // missingSamples (suena lo demás) para que la interfaz lo señale.
    std::shared_ptr<const SamplerProgram> loadInstrument (const std::string& key, int sampleRate, std::string& error);
    // Importa un ZIP (stored o deflate) en userDir. replace: sustituye una versión instalada igual o anterior.
    PackImportResult importZip (const std::string& zipPath, bool replace, const PackImportLimits& limits = {});
    // Borra un paquete importado (nunca uno de fábrica). La interfaz debe avisar antes si algún proyecto lo usa.
    bool remove (const std::string& packId, std::string& error);
    // Comprueba los SHA-256 de todos los archivos. bad: los que no coinciden o faltan.
    bool verify (const std::string& packId, std::vector<std::string>& bad, std::string& error) const;

    void setMemoryBudget (size_t bytes) { budget = bytes; }
    size_t memoryBudget() const { return budget; }

private:
    mutable std::mutex mtx;
    std::string factory, user;
    std::vector<InstalledPack> packs;
    struct CacheEntry { std::string key; int rate; std::weak_ptr<const SamplerProgram> prog; };
    std::vector<CacheEntry> cache;
    size_t budget = 160u * 1024u * 1024u;
};
PackLibrary& packLibrary();

// Para escribir paquetes (herramienta `cauce pack`): rellena files (rutas, tamaños y SHA-256) recorriendo la carpeta.
bool fillPackFiles (const std::string& dir, PackManifest& m, std::string& error);
} // namespace cs
