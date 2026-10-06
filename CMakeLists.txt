cmake_minimum_required (VERSION 3.22)
project (AIMixAssistant VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

option (AIMIX_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)
option (AIMIX_BUILD_TESTS  "Build core unit tests and benchmark" ON)
set (AIMIX_JUCE_PATH "" CACHE PATH "Use a local JUCE checkout instead of FetchContent")
set (AIMIX_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (enables the AAX format)")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE-free core: bus, DSP analysis, logic engine. Unit-testable anywhere.
add_library (aimix_core STATIC
    core/src/SharedBus.cpp
    core/src/Fft.cpp
    core/src/Loudness.cpp
    core/src/SpectralAnalysis.cpp
    core/src/StereoAnalysis.cpp
    core/src/DelayEstimator.cpp
    core/src/TrackAnalyzer.cpp
    core/src/Suggestion.cpp
    core/src/RuleEngine.cpp
    core/src/MixEngine.cpp
    core/src/SyntheticSession.cpp)
target_include_directories (aimix_core PUBLIC core/include)
find_package (Threads REQUIRED)
target_link_libraries (aimix_core PUBLIC Threads::Threads)
if (UNIX AND NOT APPLE)
    target_link_libraries (aimix_core PUBLIC rt)
endif()
if (MSVC)
    target_compile_options (aimix_core PRIVATE /W4)
else()
    target_compile_options (aimix_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wno-float-conversion -Wno-implicit-int-float-conversion)
endif()

# ---------------------------------------------------------------------------
if (AIMIX_BUILD_TESTS)
    enable_testing()
    add_executable (aimix_tests
        tests/TestMain.cpp
        tests/RingBufferTests.cpp
        tests/SharedBusTests.cpp
        tests/DspTests.cpp
        tests/AnalyzerTests.cpp
        tests/RuleEngineTests.cpp
        tests/EndToEndTests.cpp)
    target_link_libraries (aimix_tests PRIVATE aimix_core)
    add_test (NAME aimix_tests COMMAND aimix_tests)

    add_executable (aimix_bench bench/CpuBench.cpp)
    target_link_libraries (aimix_bench PRIVATE aimix_core)
endif()

# ---------------------------------------------------------------------------
if (AIMIX_BUILD_PLUGIN)
    include (cmake/Plugin.cmake)
endif()
