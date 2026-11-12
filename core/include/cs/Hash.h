#pragma once
// SHA-256 (FIPS 180-4), sin dependencias. Se usa para la integridad de los paquetes de exportación y de Cauce Link:
// quien recibe un archivo puede comprobar que es exactamente el que se generó.
#include <cstddef>
#include <cstdint>
#include <string>

namespace cs
{
class Sha256
{
public:
    Sha256();
    void update (const void* data, size_t size);
    std::string hex(); // termina el cálculo; no se puede seguir actualizando después
private:
    void block (const uint8_t* p);
    uint32_t h[8];
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;
};
std::string sha256Hex (const std::string& bytes);
// Hash de un archivo, leído por trozos. Devuelve "" si no se puede leer.
std::string sha256File (const std::string& path, long long* sizeOut = nullptr);
} // namespace cs
