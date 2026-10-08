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
// Instruments and K MASTER itself are skipped.
struct ScanOptions
{
    bool audioUnits = true;
    bool vst3 = true;
    bool vst2 = true;
    bool presets = true;
    juce::Array<juce::File> extraVst3Folders;     // for tests
    juce::Array<juce::File> extraPresetFolders;   // for tests
};

std::vector<PluginInfo> scanInstalledPlugins (const ScanOptions& options, const std::atomic<bool>* cancel = nullptr);

juce::Array<juce::File> defaultVst3Folders();
juce::Array<juce::File> defaultVst2Folders();

// Saved preset files (.aupreset, .vstpreset, FabFilter .ffp, FL .fst, Studio
// One .preset, Logic .pst...) live in folders named after the plugin. Fills
// PluginInfo::presets with their names. Factory presets built into a plugin
// can't be seen without loading it, so those are not listed.
juce::Array<juce::File> defaultPresetFolders();
void attachPresets (std::vector<PluginInfo>& plugins, const juce::Array<juce::File>& roots,
                    const std::atomic<bool>* cancel = nullptr);

// One VST3 bundle or file. Public for tests.
std::vector<PluginInfo> describeVst3 (const juce::File& bundle);

#if JUCE_MAC
std::vector<PluginInfo> scanAudioUnits (const std::atomic<bool>* cancel);   // AuScan.mm
#endif

} // namespace aimix::library
