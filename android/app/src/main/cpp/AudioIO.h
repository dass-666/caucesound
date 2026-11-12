#pragma once
// Entrada/salida de audio en Android con Oboe (AAudio en Android 8.1+, OpenSL ES como respaldo).
// Patrón dúplex: el callback de salida lee la entrada sin bloquear (read con timeout 0) y llama a Engine::process.
// Si el dispositivo de audio cambia (auriculares, USB, Bluetooth), Oboe cierra el stream con ErrorDisconnected y
// lo reabrimos en onErrorAfterClose (que Oboe ejecuta fuera del hilo de audio).
#include "cs/Engine.h"

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace csandroid
{
class AudioIO : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback
{
public:
    explicit AudioIO (cs::Engine& e) : engine (e) {}
    ~AudioIO() override { stop(); }

    bool start();               // abre y arranca la salida (y la entrada si está habilitada)
    void stop();
    // Antes de Play: si el stream no está realmente en marcha (cerrado por un error, o 'running' sin stream
    // iniciado), lo cierra y lo reabre. Si ya funciona no hace nada (no corta el audio).
    bool ensureRunning();
    bool setInputEnabled (bool on); // requiere el permiso RECORD_AUDIO concedido
    bool isRunning() const { return running.load(); }
    std::string statusJson();   // latencias, frecuencia, API, burst, xruns, reaperturas

    oboe::DataCallbackResult onAudioReady (oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose (oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool startLocked();
    bool openOutputLocked();
    bool openInputLocked();
    void closeLocked();

    cs::Engine& engine;
    std::mutex mtx;
    std::shared_ptr<oboe::AudioStream> out, in;
    std::atomic<oboe::AudioStream*> inRaw { nullptr };
    std::vector<float> inBuf;
    std::atomic<bool> running { false }, wantInput { false };
    std::atomic<int> reopenCount { 0 }, inputShortReads { 0 }, ensureRestarts { 0 };
    std::atomic<int> lastError { 0 }; // último oboe::Result recibido en onErrorAfterClose (0 = ninguno)
    std::atomic<long> callbacks { 0 };
    bool primingInput = false;
};
} // namespace csandroid
