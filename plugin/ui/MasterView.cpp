#include "MasterView.h"

namespace aimix::ui
{
void SummaryBar::paint (juce::Graphics& g)
{
    struct Tile { juce::String label, value, unit; juce::Colour colour; };
    std::vector<Tile> tiles;

    if (report != nullptr && report->hasMaster)
    {
        const auto& m = report->master.view;
        const float diff = m.integratedLufs - targetLufs;
        tiles.push_back ({ "INTEGRATED", formatLufs (m.integratedLufs), "LUFS",
                           m.integratedLufs <= -70.0f ? colours::text : (std::abs (diff) <= 3.0f ? colours::meterGreen : colours::meterAmber) });
        tiles.push_back ({ "TARGET", juce::String (targetLufs, 0), "LUFS", colours::textDim });
        tiles.push_back ({ "SHORT-TERM", formatLufs (m.shortTermLufs), "LUFS", colours::text });
        tiles.push_back ({ "PEAK", formatDb (m.maxPeakDb()), "dBFS", m.maxPeakDb() >= -1.0f ? colours::meterRed : colours::text });
        tiles.push_back ({ "CORRELATION", juce::String (m.correlation, 2), {}, m.correlation < 0.0f ? colours::meterRed : (m.correlation < 0.3f ? colours::meterAmber : colours::text) });
        const float w = std::pow (10.0f, m.sideToMidDb / 10.0f);
        tiles.push_back ({ "WIDTH", juce::String (juce::roundToInt (100.0f * w / (1.0f + w))), "%", colours::text });
    }
    else
    {
        tiles.push_back ({ "MIX BUS", "--", {}, colours::textDim });
    }

    if (report != nullptr)
    {
        tiles.push_back ({ "TRACKS", juce::String ((int) report->tracks.size()), {}, colours::text });
        tiles.push_back ({ "ENGINE", juce::String (report->lastTickMs, 2), "ms", colours::textDim });
    }

    auto area = getLocalBounds().toFloat();
    const float tileW = area.getWidth() / (float) juce::jmax<size_t> (8, tiles.size());
    for (const auto& t : tiles)
    {
        auto r = area.removeFromLeft (tileW).reduced (3.0f, 0.0f);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::textFaint);
        g.setFont (font (10.0f, true));
        g.drawText (t.label, r.reduced (10.0f, 6.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
        auto valueArea = r.reduced (10.0f, 6.0f).withTrimmedTop (14.0f);
        g.setColour (t.colour);
        g.setFont (monoFont (20.0f));
        const float vw = juce::GlyphArrangement::getStringWidth (monoFont (20.0f), t.value);
        g.drawText (t.value, valueArea, juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.setFont (font (11.0f));
        g.drawText (t.unit, valueArea.withTrimmedLeft (vw + 4.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
MasterView::MasterView()
{
    addAndMakeVisible (summary);
    addAndMakeVisible (rack);
    addAndMakeVisible (diagnostics);
    rack.onSelectionChanged = [this] (int64_t id) { diagnostics.setTrackFilter (id); if (current) rack.setReport (*current); };
    diagnostics.onDismiss = [this] (const std::string& key) { if (onDismiss) onDismiss (key); };
}

void MasterView::setReport (std::shared_ptr<const MixReport> report)
{
    if (report == nullptr || report == current)
        return;
    current = report;
    summary.setReport (report);
    rack.setReport (*report);
    diagnostics.setSuggestions (report->suggestions);
    repaint();
}

void MasterView::paint (juce::Graphics& g)
{
    if (current != nullptr && ! current->isActiveMaster)
    {
        g.setColour (colours::warning);
        g.setFont (font (12.0f, true));
        g.drawText ("Another Master Engine instance owns the bus. This one is passive.", getLocalBounds().removeFromBottom (18), juce::Justification::centred);
    }
}

void MasterView::resized()
{
    auto r = getLocalBounds();
    summary.setBounds (r.removeFromTop (58));
    r.removeFromTop (12);
    const int rackWidth = juce::roundToInt ((float) r.getWidth() * 0.56f);
    rack.setBounds (r.removeFromLeft (rackWidth));
    r.removeFromLeft (14);
    diagnostics.setBounds (r);
}

} // namespace aimix::ui
