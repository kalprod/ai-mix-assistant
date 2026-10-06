#pragma once

#include "Theme.h"
#include "aimix/MixEngine.h"

#include <functional>
#include <map>

namespace aimix::ui
{
// One vertical strip per Listener: role colour, name, L/R RMS+peak meters,
// LUFS readouts, mini spectrum, phase-correlation and width, issue badge.
class ChannelStrip final : public juce::Component
{
public:
    struct Issues { int count = 0; Severity worst = Severity::Info; };

    void setData (const TrackSummary& summary, Issues issues, bool isMaster, bool selected);
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }

    static constexpr int kWidth = 118;

private:
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float rmsDb, float peakDb) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>) const;
    void drawCorrelation (juce::Graphics&, juce::Rectangle<float>) const;

    TrackSummary data;
    Issues issues;
    bool master = false, isSelected = false;
};

class ChannelRack final : public juce::Component
{
public:
    ChannelRack();

    void setReport (const MixReport& report);
    void resized() override;
    void paint (juce::Graphics&) override;

    // Fires with the track id clicked, or -1 when the selection is cleared.
    std::function<void (int64_t)> onSelectionChanged;
    int64_t getSelectedTrack() const noexcept { return selectedTrack; }

private:
    void layoutStrips();

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
    int64_t selectedTrack = -1;
    int lastHeight = 0;
};

} // namespace aimix::ui
