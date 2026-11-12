#include "Test.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <new>

namespace cstest
{
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int failures = 0;
std::atomic<long> allocationCount { 0 };
std::atomic<bool> countAllocations { false };
static int currentFailures = 0;

void fail (const char* file, int line, const std::string& msg)
{
    ++failures;
    ++currentFailures;
    if (currentFailures <= 10) std::printf ("    FALLO %s:%d %s\n", file, line, msg.c_str());
}

std::string tempDir (const std::string& name)
{
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / ("cs_test_" + name);
    std::error_code ec;
    fs::remove_all (p, ec);
    fs::create_directories (p, ec);
    return p.string();
}
} // namespace cstest

void* operator new (std::size_t n)
{
    if (cstest::countAllocations.load (std::memory_order_relaxed)) cstest::allocationCount.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, passed = 0;
    for (auto& t : cstest::registry())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos) continue;
        cstest::currentFailures = 0;
        std::printf ("[ RUN  ] %s\n", t.name);
        const auto t0 = std::chrono::steady_clock::now();
        try { t.fn(); }
        catch (const std::exception& e) { cstest::fail (__FILE__, __LINE__, std::string ("excepción: ") + e.what()); }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        ++run;
        if (cstest::currentFailures == 0) { ++passed; std::printf ("[  OK  ] %s (%.0f ms)\n", t.name, ms); }
        else std::printf ("[FALLA ] %s (%d fallos)\n", t.name, cstest::currentFailures);
    }
    std::printf ("\n%d/%d pruebas superadas\n", passed, run);
    return passed == run && run > 0 ? 0 : 1;
}
