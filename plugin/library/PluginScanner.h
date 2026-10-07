#pragma once

#include <juce_core/juce_core.h>

#include "aimix/PluginLibrary.h"

#include <atomic>
#include <vector>

namespace aimix::library
{
// Finds the effect plugins installed on this machine without loading any of
// them (loading a plugin to read its name can crash or show dialogs):
//
//   Audio Units  macOS component registry (name, maker, version)
//   VST3         standard folders; name/vendor/category from the bundle's
//                moduleinfo.json, else its Info.plist, else the file name
//   VST          standard folders; name from the file name
//
// Instruments and AI Mix Assistant itself are skipped.
struct ScanOptions
{
    bool audioUnits = true;
    bool vst3 = true;
    bool vst2 = true;
    juce::Array<juce::File> extraVst3Folders;   // for tests
};

std::vector<PluginInfo> scanInstalledPlugins (const ScanOptions& options, const std::atomic<bool>* cancel = nullptr);

juce::Array<juce::File> defaultVst3Folders();
juce::Array<juce::File> defaultVst2Folders();

// One VST3 bundle or file. Public for tests.
std::vector<PluginInfo> describeVst3 (const juce::File& bundle);

#if JUCE_MAC
std::vector<PluginInfo> scanAudioUnits (const std::atomic<bool>* cancel);   // AuScan.mm
#endif

} // namespace aimix::library
