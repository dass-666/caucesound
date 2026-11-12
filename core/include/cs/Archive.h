#pragma once
// Exportar/importar un proyecto completo como un único archivo ZIP (extensión .caucesound) para copias de seguridad,
// pasar proyectos entre apps o dispositivos y compartirlos. Sin dependencias: ZIP «stored» (sin compresión; el audio
// WAV apenas se comprime) con CRC-32, legible por cualquier descompresor. Límite: 4 GB por archivo (sin ZIP64).
//
// Contenido: project.json + los WAV que el proyecto referencia (audio/...). Nada más: ni copias .bak ni grabaciones
// sin terminar. La importación solo acepta esas rutas (protección contra «zip slip»), comprueba los CRC, valida el
// proyecto con el mismo código que al abrirlo y le da un id nuevo; si algo falla no deja nada a medias.
#include <string>

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

// Extrae archivePath en destFolder (que no debe existir) y lo deja listo para openProject(). Solo ZIP «stored».
ArchiveResult importProjectArchive (const std::string& archivePath, const std::string& destFolder);
} // namespace cs
