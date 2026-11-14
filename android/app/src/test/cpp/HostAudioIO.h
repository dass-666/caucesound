#pragma once
// Sustituto de AudioIO (Oboe) para ejecutar el puente JNI REAL en la JVM de escritorio (pruebas Robolectric de
// extremo a extremo, hotfix 0.9.2). Un hilo llama a Engine::process con bloques de 1024 frames a ritmo de tiempo
// real, igual que lo haría el callback de Oboe en el teléfono; no hay altavoz ni micrófono (entrada en silencio).
// Solo se compila con -DCS_BUILD_HOST_JNI=ON; el APK sigue usando AudioIO.cpp con Oboe.
#include "cs/Engine.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace csandroid
{
class AudioIO
{
public:
    explicit AudioIO (cs::Engine& e) : engine (e) {}
    ~AudioIO() { stop(); }

    bool start()
    {
        std::lock_guard<std::mutex> l (mtx);
        if (running.load()) return true;
        running.store (true);
        worker = std::thread ([this] { loop(); });
        return true;
    }
    void stop()
    {
        std::lock_guard<std::mutex> l (mtx);
        if (! running.exchange (false)) return;
        if (worker.joinable()) worker.join();
    }
    bool ensureRunning() { return start(); }
    bool setInputEnabled (bool on) { input.store (on); return true; }
    bool setEchoCancel (bool on) { echo.store (on); return true; }
    bool isRunning() const { return running.load(); }
    std::string statusJson()
    {
        return std::string ("{\"api\":\"host\",\"sampleRate\":48000,\"framesPerBurst\":1024,\"running\":") + (running.load() ? "true" : "false")
             + ",\"callbacks\":" + std::to_string (callbacks.load()) + ",\"input\":" + (input.load() ? "true" : "false") + "}";
    }

private:
    void loop()
    {
        constexpr int n = 1024;
        std::vector<float> in ((size_t) n * 2, 0.0f), out ((size_t) n * 2, 0.0f);
        auto next = std::chrono::steady_clock::now();
        const auto period = std::chrono::microseconds ((long long) n * 1000000 / 48000);
        while (running.load())
        {
            engine.process (in.data(), input.load() ? 1 : 0, out.data(), 2, n);
            ++callbacks;
            next += period;
            std::this_thread::sleep_until (next);
        }
    }

    cs::Engine& engine;
    std::mutex mtx;
    std::thread worker;
    std::atomic<bool> running { false }, input { false }, echo { false };
    std::atomic<long> callbacks { 0 };
};
} // namespace csandroid
