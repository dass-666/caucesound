#pragma once
// Marco de pruebas mínimo (sin dependencias). Cada prueba imprime sus mediciones con "measure:".
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace cstest
{
struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry();
struct Registrar { Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };
extern int failures;
void fail (const char* file, int line, const std::string& msg);

// Contador de reservas de memoria (operator new sustituido en TestMain.cpp)
extern std::atomic<long> allocationCount;
extern std::atomic<bool> countAllocations;

std::string tempDir (const std::string& name); // carpeta temporal vacía y portable
} // namespace cstest

#define CS_CAT2(a, b) a##b
#define CS_CAT(a, b) CS_CAT2(a, b)
#define TEST(name) \
    static void name(); \
    static cstest::Registrar CS_CAT(reg_, name) (#name, name); \
    static void name()
#define CHECK(cond) do { if (! (cond)) cstest::fail (__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_MSG(cond, msg) do { if (! (cond)) cstest::fail (__FILE__, __LINE__, std::string ("CHECK(" #cond "): ") + (msg)); } while (0)
#define CHECK_NEAR(a, b, tol) do { const double _a = (a), _b = (b); if (! (std::fabs (_a - _b) <= (tol))) \
    cstest::fail (__FILE__, __LINE__, std::string (#a " ~= " #b ": ") + std::to_string (_a) + " vs " + std::to_string (_b)); } while (0)
#define MEASURE(...) do { std::printf ("    measure: "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)
