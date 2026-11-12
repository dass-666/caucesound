#pragma once
// Cola circular sin bloqueo de un productor y un consumidor (p. ej. hilo de audio -> hilo de disco).
// Capacidad fija reservada en el constructor; push/pop no reservan memoria ni bloquean.
#include <atomic>
#include <cstddef>
#include <vector>

namespace cs
{
template <typename T>
class SpscRing
{
public:
    explicit SpscRing (size_t capacity) : buf (capacity + 1) {}

    size_t capacity() const { return buf.size() - 1; }

    size_t availableToRead() const
    {
        const size_t w = writeIdx.load (std::memory_order_acquire), r = readIdx.load (std::memory_order_relaxed);
        return w >= r ? w - r : w + buf.size() - r;
    }
    size_t availableToWrite() const { return capacity() - availableToReadFromProducer(); }

    // Productor. Escribe todo o nada; devuelve false si no cabe.
    bool push (const T* data, size_t n)
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed), r = readIdx.load (std::memory_order_acquire);
        const size_t used = w >= r ? w - r : w + buf.size() - r;
        if (n > capacity() - used) return false;
        size_t idx = w;
        for (size_t i = 0; i < n; ++i) { buf[idx] = data[i]; if (++idx == buf.size()) idx = 0; }
        writeIdx.store (idx, std::memory_order_release);
        return true;
    }

    // Consumidor. Lee hasta n elementos; devuelve los leídos.
    size_t pop (T* out, size_t n)
    {
        const size_t r = readIdx.load (std::memory_order_relaxed), w = writeIdx.load (std::memory_order_acquire);
        const size_t avail = w >= r ? w - r : w + buf.size() - r;
        const size_t count = n < avail ? n : avail;
        size_t idx = r;
        for (size_t i = 0; i < count; ++i) { out[i] = buf[idx]; if (++idx == buf.size()) idx = 0; }
        readIdx.store (idx, std::memory_order_release);
        return count;
    }

private:
    size_t availableToReadFromProducer() const
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed), r = readIdx.load (std::memory_order_acquire);
        return w >= r ? w - r : w + buf.size() - r;
    }
    std::vector<T> buf;
    alignas (64) std::atomic<size_t> writeIdx { 0 };
    alignas (64) std::atomic<size_t> readIdx { 0 };
};
} // namespace cs
