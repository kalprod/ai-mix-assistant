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

    // How this DAW names its insert slots, and how to add plugins to them.
    struct Daw
    {
        juce::String slotWord = "Insert slot";   // "Mixer insert slot", "Audio FX slot"...
        juce::String howTo;                      // one line shown above the list
    };
    void setDaw (const Daw& d) { daw = d; repaint(); rows.repaint(); }
    static Daw dawFor (const juce::String& hostName);   // "FL Studio", "Studio One", "Logic"...

    // Slots the user has ticked as added, as "slotId=plugin name". A tick is
    // dropped when the suggestion for that slot changes.
    void setAdded (const juce::StringArray& added) { addedKeys = added; rows.repaint(); }
    const juce::StringArray& getAdded() const noexcept { return addedKeys; }
    std::function<void (const juce::StringArray&)> onAddedChanged;
    static juce::String addedKey (const ChainSlot& s) { return juce::String (s.id) + "=" + juce::String (s.pickName); }

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
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> tickBox (size_t index) const;
        float rowHeight (const ChainSlot& s, float width) const;
        ChainPanel& owner;
    };

    void recompute();
    juce::String headline() const;

    std::vector<PluginInfo> library;
    ChainContext context;
    bool hasContext = false;
    ChainRecommendation rec;
    MixStyle styleShown;
    juce::String status;
    Daw daw;
    juce::StringArray addedKeys;
    bool scanning = false;

    juce::TextButton rescanButton { "Rescan plugins" };
    juce::Viewport viewport;
    Rows rows { *this };
};

} // namespace aimix::ui
