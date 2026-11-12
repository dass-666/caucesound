#include "cs/Jobs.h"

#include <exception>

namespace cs
{
JobRunner::JobRunner() { th = std::thread ([this] { loop(); }); }

JobRunner::~JobRunner()
{
    {
        std::lock_guard<std::mutex> l (m);
        quit = true;
        for (auto& j : queue) { j->ctx->cancel = true; j->info.state = "cancelled"; }
        queue.clear();
        if (running) running->ctx->cancel = true;
    }
    cv.notify_all();
    if (th.joinable()) th.join();
}

std::string JobRunner::submit (const std::string& type, const std::string& projectId, const std::string& targetId, Work work)
{
    std::lock_guard<std::mutex> l (m);
    if (quit || (int) queue.size() >= kMaxQueued) return {};
    auto j = std::make_shared<Job>();
    j->info.id = "job_" + std::to_string (++counter);
    j->info.type = type;
    j->info.projectId = projectId;
    j->info.targetId = targetId;
    j->work = std::move (work);
    j->ctx = std::make_shared<JobContext>();
    all[j->info.id] = j;
    queue.push_back (j);
    cv.notify_all();
    return j->info.id;
}

bool JobRunner::cancel (const std::string& id)
{
    std::lock_guard<std::mutex> l (m);
    auto it = all.find (id);
    if (it == all.end()) return false;
    auto& j = it->second;
    if (j->info.state == "queued")
    {
        j->info.state = "cancelled";
        for (auto q = queue.begin(); q != queue.end(); ++q) if (*q == j) { queue.erase (q); break; }
        finishedOrder.push_back (id);
        idleCv.notify_all();
    }
    else if (j->info.state == "running") j->ctx->cancel = true;
    return true;
}

void JobRunner::cancelProject (const std::string& projectId)
{
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> l (m);
        for (auto& [id, j] : all) if (j->info.projectId == projectId && (j->info.state == "queued" || j->info.state == "running")) ids.push_back (id);
    }
    for (auto& id : ids) cancel (id);
}

void JobRunner::cancelAll()
{
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> l (m);
        for (auto& [id, j] : all) if (j->info.state == "queued" || j->info.state == "running") ids.push_back (id);
    }
    for (auto& id : ids) cancel (id);
}

void JobRunner::waitIdle()
{
    std::unique_lock<std::mutex> l (m);
    idleCv.wait (l, [this] { return queue.empty() && ! running; });
}

bool JobRunner::info (const std::string& id, JobInfo& out) const
{
    std::lock_guard<std::mutex> l (m);
    auto it = all.find (id);
    if (it == all.end()) return false;
    out = it->second->info;
    if (out.state == "running") out.progress = it->second->ctx->progress.load();
    return true;
}

std::vector<JobInfo> JobRunner::list() const
{
    std::lock_guard<std::mutex> l (m);
    std::vector<JobInfo> v;
    for (auto& [id, j] : all)
    {
        v.push_back (j->info);
        if (j->info.state == "running") v.back().progress = j->ctx->progress.load();
    }
    return v;
}

int JobRunner::pending() const
{
    std::lock_guard<std::mutex> l (m);
    return (int) queue.size() + (running ? 1 : 0);
}

void JobRunner::loop()
{
    for (;;)
    {
        std::shared_ptr<Job> j;
        {
            std::unique_lock<std::mutex> l (m);
            cv.wait (l, [this] { return quit || ! queue.empty(); });
            if (quit) return;
            j = queue.front();
            queue.pop_front();
            j->info.state = "running";
            running = j;
        }
        std::string result, error;
        bool ok = false;
        try { ok = j->work (*j->ctx, result, error); }
        catch (const std::exception& e) { error = std::string ("error interno: ") + e.what(); }
        catch (...) { error = "error interno"; }
        {
            std::lock_guard<std::mutex> l (m);
            // Si el trabajo llegó a aplicar su resultado, cuenta como hecho aunque la cancelación llegara un instante después.
            if (ok) { j->info.state = "done"; j->info.result = result; j->info.progress = 1; }
            else if (j->ctx->cancel.load()) { j->info.state = "cancelled"; }
            else { j->info.state = "failed"; j->info.error = error.empty() ? "falló" : error; }
            j->work = nullptr; // suelta lo que capturó (audio, rutas)
            running.reset();
            finishedOrder.push_back (j->info.id);
            while ((int) finishedOrder.size() > kKeepFinished) { all.erase (finishedOrder.front()); finishedOrder.pop_front(); }
        }
        idleCv.notify_all();
    }
}
} // namespace cs
