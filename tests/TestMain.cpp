#include "TestFramework.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace aimixtest
{
std::atomic<long long> allocationCount { 0 };
static int failures = 0;
static const char* currentTest = "";

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

void reportFailure (const char* file, int line, const std::string& message)
{
    ++failures;
    std::printf ("    FAIL [%s] %s:%d  %s\n", currentTest, file, line, message.c_str());
}
}

// Count every heap allocation in the process.
void* operator new (std::size_t size)
{
    aimixtest::allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // a crash must not swallow the last results
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& t : aimixtest::registry())
    {
        if (filter != nullptr && std::strstr (t.name, filter) == nullptr)
            continue;
        aimixtest::currentTest = t.name;
        const int before = aimixtest::failures;
        const auto start = std::chrono::steady_clock::now();
        t.fn();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        std::printf ("%s %-62s %8.1f ms\n", aimixtest::failures == before ? "[PASS]" : "[FAIL]", t.name, ms);
        ++run;
    }
    std::printf ("\n%d tests, %d failed checks\n", run, aimixtest::failures);
    return aimixtest::failures == 0 ? 0 : 1;
}
