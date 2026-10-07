#pragma once

#include "ChainPanel.h"
#include "aimix/TrackAnalyzer.h"

namespace aimix::ui
{
// View for a Listener instance: its bus status and meters on top, the
// suggested plugin chain for this track below.
class ListenerView final : public juce::Component
{
public:
    ListenerView() { addAndMakeVisible (chain); }

    struct Status
    {
        juce::String trackName;
        juce::String roleText;     // "Kick (detected)", "Auto: listening...", or the chosen role
        TrackRole role = TrackRole::Unknown;
        int slot = -1;
        bool sharedMemory = false;
        bool masterOnline = false;
        uint64_t framesSent = 0, framesDropped = 0;
        float rmsDb[2] { -120, -120 }, peakDb[2] { -120, -120 };
        float momentary = -144, shortTerm = -144, integrated = -144;
        float correlation = 0, sideToMidDb = -100;
    };

    void setStatus (const Status& s) { status = s; repaint (0, 0, getWidth(), kMetersHeight); }
    ChainPanel& getChainPanel() noexcept { return chain; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kMetersHeight = 262;

    Status status;
    ChainPanel chain;
};

} // namespace aimix::ui
