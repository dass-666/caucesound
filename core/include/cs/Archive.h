#pragma once
// Exportar/importar un proyecto completo como un único archivo ZIP (extensión .caucesound) para copias de seguridad,
// pasar proyectos entre apps o dispositivos y compartirlos. Sin dependencias: ZIP «stored» (sin compresión; el audio
// WAV apenas se comprime) con CRC-32, legible por cualquier descompresor. Límite: 4 GB por archivo (sin ZIP64).
//
// Contenido: project.json + los WAV que el proyecto referencia (audio/...). Nada más: ni copias .bak ni grabaciones
// sin terminar. La importación solo acepta esas rutas (protección contra «zip slip»), comprueba los CRC, valida el
// proyecto con el mismo código que al abrirlo y le da un id nuevo; si algo falla no deja nada a medias.
#include <string>
#include <vector>

namespace cs
{
struct ArchiveResult
{
    bool ok = true;
    std::string error;
    std::string name;  // nombre del proyecto importado
    int files = 0;     // archivos escritos o extraídos
    long long bytes = 0;
};

// folder: carpeta de un proyecto guardado (cerrado o guardado justo antes). outPath: archivo a crear (se escribe en
// un temporal y se renombra al final).
ArchiveResult exportProjectArchive (const std::string& folder, const std::string& outPath);

// ZIP «stored» genérico (lo usa también la exportación para DAW). entries: nombre dentro del ZIP y archivo de origen.
// Escribe en outPath + ".part" y renombra al final; comprueba que ningún archivo cambie mientras se copia.
struct ZipSource { std::string name, path; };
ArchiveResult writeStoredZip (const std::vector<ZipSource>& entries, const std::string& outPath);
// Lista un ZIP «stored» comprobando el CRC de cada entrada. Para verificar lo exportado.
struct ZipInfo { std::string name; long long size = 0, dataOffset = 0; };
bool listStoredZip (const std::string& path, std::vector<ZipInfo>& out, std::string& error);
bool readStoredZipEntry (const std::string& path, const ZipInfo& info, std::string& bytes);

// Extrae archivePath en destFolder (que no debe existir) y lo deja listo para openProject(). Solo ZIP «stored».
ArchiveResult importProjectArchive (const std::string& archivePath, const std::string& destFolder);
} // namespace cs
