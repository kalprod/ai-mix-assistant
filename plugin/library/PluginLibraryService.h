#pragma once

#include <juce_events/juce_events.h>

#include "PluginScanner.h"

#include <mutex>

namespace aimix::library
{
// The scanned, tagged plugin list, shared by every AI Mix Assistant instance
// in the process (use through juce::SharedResourcePointer).
//
// Saved as JSON so later sessions open instantly:
//   macOS    ~/Library/Application Support/AI Mix Assistant/plugin-library.json
//   Windows  %APPDATA%\AI Mix Assistant\plugin-library.json
//   Linux    ~/.config/AI Mix Assistant/plugin-library.json
// A rescan keeps your own changes (favourites, tags you corrected).
class PluginLibraryService final : private juce::Thread,
                                   private juce::AsyncUpdater
{
public:
    PluginLibraryService();
    ~PluginLibraryService() override;

    std::vector<PluginInfo> getPlugins() const;
    bool isScanning() const noexcept { return isThreadRunning(); }
    bool hasScanned() const;
    juce::String getStatusText() const;   // "Found 143 plugins, 22 analog models"

    void rescan();   // background thread; listeners hear when it's done

    juce::ChangeBroadcaster changes;   // message thread, after a load or scan

    static juce::File libraryFile();
    static juce::var toJson (const std::vector<PluginInfo>& plugins, int64_t scannedAtMs);
    static std::vector<PluginInfo> fromJson (const juce::var& json, int64_t* scannedAtMs = nullptr);

    // Carries favourites and user-corrected tags from an older list into a new scan.
    static void keepUserChanges (std::vector<PluginInfo>& fresh, const std::vector<PluginInfo>& old);

private:
    void run() override;
    void handleAsyncUpdate() override;

    mutable std::mutex lock;
    std::vector<PluginInfo> plugins;
    int64_t scannedAtMs = 0;
    std::atomic<bool> cancel { false };
};

} // namespace aimix::library
