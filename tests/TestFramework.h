#pragma once

// Minimal dependency-free test harness (keeps the core buildable with nothing
// but a C++17 compiler; swap for Catch2/GoogleTest if preferred).

#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace aimixtest
{
struct TestCase { const char* name; std::function<void()> fn; };

std::vector<TestCase>& registry();
void reportFailure (const char* file, int line, const std::string& message);

// Global allocation counter (operator new is replaced in TestMain.cpp) used to
// prove the audio-thread path never allocates.
extern std::atomic<long long> allocationCount;

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};
}

#define AIMIX_CONCAT_(a, b) a##b
#define AIMIX_CONCAT(a, b) AIMIX_CONCAT_(a, b)

#define TEST_CASE(name)                                                            \
    static void AIMIX_CONCAT (test_fn_, __LINE__)();                               \
    static aimixtest::Registrar AIMIX_CONCAT (test_reg_, __LINE__) (name, &AIMIX_CONCAT (test_fn_, __LINE__)); \
    static void AIMIX_CONCAT (test_fn_, __LINE__)()

#define CHECK(cond)                                                                \
    do { if (! (cond)) aimixtest::reportFailure (__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_NEAR(actual, expected, tol)                                          \
    do {                                                                           \
        const double a_ = (double) (actual), e_ = (double) (expected);             \
        if (! (std::abs (a_ - e_) <= (double) (tol))) {                            \
            char buf_[256];                                                        \
            std::snprintf (buf_, sizeof (buf_), "CHECK_NEAR(%s) got %.6g expected %.6g +- %.3g", #actual, a_, e_, (double) (tol)); \
            aimixtest::reportFailure (__FILE__, __LINE__, buf_);                   \
        }                                                                          \
    } while (0)

#define REQUIRE(cond)                                                              \
    do { if (! (cond)) { aimixtest::reportFailure (__FILE__, __LINE__, "REQUIRE(" #cond ")"); return; } } while (0)
