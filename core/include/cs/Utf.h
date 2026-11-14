#pragma once
// Conversión UTF-16 <-> UTF-8 estricta para el puente con la interfaz (JNI hoy; el mismo problema existe en cualquier
// puente que entregue UTF-16). JNI ofrece GetStringUTFChars/NewStringUTF, pero trabajan en «UTF-8 modificado»: un
// emoji llega como dos secuencias de 3 bytes (par subrogado), que no es UTF-8 válido y el analizador de JSON rechaza
// la petición entera (auditoría 0.10: un proyecto llamado «Beat 🔥» no se podía crear en una JVM estándar).
// Aquí la entrada se convierte desde UTF-16 y la salida hacia UTF-16, sin depender de cómo lo haga cada máquina
// virtual. Lo que no es válido (subrogado suelto, bytes rotos) se sustituye por U+FFFD en vez de fallar.
#include <cstddef>
#include <cstdint>
#include <string>

namespace cs
{
inline void appendUtf8 (std::string& out, uint32_t cp)
{
    if (cp < 0x80) out += (char) cp;
    else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
    else { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
}

inline std::string utf16ToUtf8 (const char16_t* s, size_t n)
{
    std::string out;
    out.reserve (n + n / 2);
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint32_t) s[i + 1] - 0xDC00);
            ++i;
        }
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD; // subrogado suelto
        appendUtf8 (out, c);
    }
    return out;
}

inline std::u16string utf8ToUtf16 (const std::string& s)
{
    std::u16string out;
    out.reserve (s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n)
    {
        const uint8_t b = (uint8_t) s[i];
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        auto cont = [&] (size_t k) { return i + k < n && ((uint8_t) s[i + k] & 0xC0) == 0x80; };
        if (b < 0x80) cp = b;
        else if (b >= 0xC2 && b <= 0xDF && cont (1)) { cp = ((uint32_t) (b & 0x1F) << 6) | ((uint8_t) s[i + 1] & 0x3F); len = 2; }
        else if (b >= 0xE0 && b <= 0xEF && cont (1) && cont (2))
        {
            const uint32_t v = ((uint32_t) (b & 0x0F) << 12) | ((uint32_t) ((uint8_t) s[i + 1] & 0x3F) << 6) | ((uint8_t) s[i + 2] & 0x3F);
            if (v >= 0x800 && ! (v >= 0xD800 && v <= 0xDFFF)) { cp = v; len = 3; } // ni forma larga ni subrogados
        }
        else if (b >= 0xF0 && b <= 0xF4 && cont (1) && cont (2) && cont (3))
        {
            const uint32_t v = ((uint32_t) (b & 0x07) << 18) | ((uint32_t) ((uint8_t) s[i + 1] & 0x3F) << 12)
                               | ((uint32_t) ((uint8_t) s[i + 2] & 0x3F) << 6) | ((uint8_t) s[i + 3] & 0x3F);
            if (v >= 0x10000 && v <= 0x10FFFF) { cp = v; len = 4; }
        }
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            out += (char16_t) (0xD800 + (cp >> 10));
            out += (char16_t) (0xDC00 + (cp & 0x3FF));
        }
        else out += (char16_t) cp;
        i += len;
    }
    return out;
}
} // namespace cs
