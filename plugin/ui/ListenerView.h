#pragma once

#include "Theme.h"
#include "aimix/TrackAnalyzer.h"

namespace aimix::ui
{
// Compact view for a Listener instance: its own meters and bus status.
class ListenerView final : public juce::Component
{
public:
    struct Status
    {
        juce::String trackName;
        TrackRole role = TrackRole::Unknown;
        int slot = -1;
        bool sharedMemory = false;
        bool masterOnline = false;
        uint64_t framesSent = 0, framesDropped = 0;
        float rmsDb[2] { -120, -120 }, peakDb[2] { -120, -120 };
        float momentary = -144, shortTerm = -144, integrated = -144;
        float correlation = 0, sideToMidDb = -100;
    };

    void setStatus (const Status& s) { status = s; repaint(); }
    void paint (juce::Graphics&) override;

private:
    Status status;
};

} // namespace aimix::ui
