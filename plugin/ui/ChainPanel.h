#pragma once

#include "Theme.h"
#include "aimix/ChainRecommender.h"

#include <functional>

namespace aimix::ui
{
// The suggested insert chain for this track (or the mix bus), picked from the
// plugins installed on this computer, analog models first. One row per slot:
// what it's for, which of your plugins to use, which preset to start from,
// and 2-4 knob starting points.
class ChainPanel final : public juce::Component
{
public:
    ChainPanel();

    void setLibrary (std::vector<PluginInfo> plugins, const juce::String& statusText, bool scanning);
    void setContext (const ChainContext& ctx);
    const ChainRecommendation& getRecommendation() const noexcept { return rec; }

    std::function<void()> onRescan;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kHeaderHeight = 44;

    class Rows final : public juce::Component
    {
    public:
        explicit Rows (ChainPanel& p) : owner (p) {}
        int heightFor (int width) const;
        void paint (juce::Graphics&) override;

    private:
        float rowHeight (const ChainSlot& s, float width) const;
        ChainPanel& owner;
    };

    void recompute();
    juce::String headline() const;

    std::vector<PluginInfo> library;
    ChainContext context;
    bool hasContext = false;
    ChainRecommendation rec;
    juce::String status;
    bool scanning = false;

    juce::TextButton rescanButton { "Rescan plugins" };
    juce::Viewport viewport;
    Rows rows { *this };
};

} // namespace aimix::ui
