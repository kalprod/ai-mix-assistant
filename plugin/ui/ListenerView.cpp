#include "ListenerView.h"

namespace aimix::ui
{
void ListenerView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // status card
    auto card = r.removeFromTop (92.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (card, 8.0f);
    auto c = card.reduced (16.0f, 12.0f);

    const auto dot = status.masterOnline ? colours::meterGreen : colours::meterAmber;
    g.setColour (dot);
    g.fillEllipse (c.getX(), c.getY() + 5.0f, 10.0f, 10.0f);
    g.setColour (colours::text);
    g.setFont (font (16.0f, true));
    g.drawText (status.masterOnline ? "Streaming to Master Engine" : "Waiting for a Master Engine",
                c.withTrimmedLeft (18.0f).removeFromTop (20.0f), juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.setFont (font (14.0f, true));
    g.drawText (status.roleText, c.withHeight (20.0f), juce::Justification::centredRight);

    g.setColour (colours::textDim);
    g.setFont (font (12.5f));
    juce::String line = status.slot >= 0 ? "Track ID " + juce::String (status.slot) : juce::String ("Bus full: not connected");
    line << "   |   " << (status.sharedMemory ? "shared-memory bus" : "in-process bus")
         << "   |   frames sent " << juce::String ((juce::int64) status.framesSent)
         << ", dropped " << juce::String ((juce::int64) status.framesDropped);
    g.drawText (line, c.withTrimmedTop (28.0f).removeFromTop (18.0f), juce::Justification::centredLeft);
    g.setColour (colours::textFaint);
    g.drawText (status.masterOnline ? "Audio passes through untouched. Suggestions appear on the Master Engine instance."
                                    : "Insert one instance on the mix bus and set its Mode to Master Engine.",
                c.withTrimmedTop (50.0f).removeFromTop (18.0f), juce::Justification::centredLeft);

    r.removeFromTop (14.0f);

    // stat tiles
    struct Tile { juce::String label, value; juce::Colour colour; };
    const float w = std::pow (10.0f, status.sideToMidDb / 10.0f);
    const float peak = juce::jmax (status.peakDb[0], status.peakDb[1]);
    const Tile tiles[] = {
        { "RMS L / R", formatDb (status.rmsDb[0]) + " / " + formatDb (status.rmsDb[1]), colours::text },
        { "PEAK", formatDb (peak) + " dBFS", peak >= -0.3f ? colours::meterRed : colours::text },
        { "LUFS M / S", formatLufs (status.momentary) + " / " + formatLufs (status.shortTerm), colours::text },
        { "LUFS INTEGRATED", formatLufs (status.integrated), colours::text },
        { "PHASE CORRELATION", juce::String (status.correlation, 2), status.correlation < 0 ? colours::meterRed : colours::text },
        { "STEREO WIDTH", juce::String (juce::roundToInt (100.0f * w / (1.0f + w))) + " %", colours::text },
    };

    const float tileW = r.getWidth() / 3.0f;
    for (int i = 0; i < 6; ++i)
    {
        auto t = juce::Rectangle<float> (r.getX() + tileW * (float) (i % 3), r.getY() + 76.0f * (float) (i / 3), tileW, 70.0f).reduced (4.0f, 0.0f);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (t, 8.0f);
        g.setColour (colours::textFaint);
        g.setFont (font (10.5f, true));
        g.drawText (tiles[i].label, t.reduced (14.0f, 10.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
        g.setColour (tiles[i].colour);
        g.setFont (monoFont (21.0f));
        g.drawText (tiles[i].value, t.reduced (14.0f, 10.0f).withTrimmedTop (18.0f), juce::Justification::centredLeft);
    }
}

} // namespace aimix::ui
