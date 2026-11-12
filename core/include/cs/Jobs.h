#pragma once
// Trabajos offline (misión 0.5): cola con un hilo de trabajo para todo lo que no debe ejecutarse ni en el callback de
// audio ni bloqueando la sesión (procesar una toma con VocalShift, analizar un beat...). Cada trabajo:
//   - lleva el id del proyecto para el que se lanzó: el resultado solo se aplica si ese proyecto sigue abierto;
//   - informa de progreso real (0..1) y se puede cancelar (el trabajo consulta 'cancel' entre bloques);
//   - termina en un estado final (done / failed / cancelled) con su resultado o su error.
// Un solo hilo a propósito: en un teléfono, dos procesados pesados a la vez compiten con el audio en vivo.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cs
{
struct JobInfo
{
    std::string id, type, projectId, targetId;
    std::string state = "queued"; // queued | running | done | failed | cancelled
    float progress = 0;
    std::string error;
    std::string result;           // JSON con el resultado (solo en "done")
};

struct JobContext
{
    std::atomic<float> progress { 0 };
    std::atomic<bool> cancel { false };
};

class JobRunner
{
public:
    // Devuelve true si terminó bien (result = JSON); false con 'error'. Si ctx.cancel está activo al volver, el
    // trabajo cuenta como cancelado y su resultado se descarta.
    using Work = std::function<bool (JobContext& ctx, std::string& result, std::string& error)>;

    JobRunner();
    ~JobRunner(); // cancela todo y espera al hilo

    std::string submit (const std::string& type, const std::string& projectId, const std::string& targetId, Work work);
    bool cancel (const std::string& id);
    void cancelProject (const std::string& projectId); // al cerrar o cambiar de proyecto
    void cancelAll();
    void waitIdle();                                    // bloquea hasta que no quede nada en cola ni en curso
    bool info (const std::string& id, JobInfo& out) const;
    std::vector<JobInfo> list() const;
    int pending() const;

    static constexpr int kMaxQueued = 4;   // más trabajos en cola se rechazan (submit devuelve "")
    static constexpr int kKeepFinished = 16;

private:
    struct Job { JobInfo info; Work work; std::shared_ptr<JobContext> ctx; };
    void loop();

    mutable std::mutex m;
    std::condition_variable cv, idleCv;
    std::deque<std::shared_ptr<Job>> queue;
    std::shared_ptr<Job> running;
    std::map<std::string, std::shared_ptr<Job>> all;
    std::deque<std::string> finishedOrder;
    bool quit = false;
    unsigned counter = 0;
    std::thread th;
};
} // namespace cs
