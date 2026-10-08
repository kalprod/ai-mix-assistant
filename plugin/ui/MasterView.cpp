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
                           m.integratedLufs <= -70.0f ? colours::screenText : (std::abs (diff) <= 3.0f ? colours::meterGreen : colours::meterAmber) });
        tiles.push_back ({ "TARGET", juce::String (targetLufs, std::abs (targetLufs - std::round (targetLufs)) > 0.01f ? 1 : 0), "LUFS", colours::screenDim });
        tiles.push_back ({ "SHORT-TERM", formatLufs (m.shortTermLufs), "LUFS", colours::screenText });
        tiles.push_back ({ "PEAK", formatDb (m.maxPeakDb()), "dBFS", m.maxPeakDb() >= -1.0f ? colours::meterRed : colours::screenText });
        tiles.push_back ({ "CORRELATION", juce::String (m.correlation, 2), {}, m.correlation < 0.0f ? colours::meterRed : (m.correlation < 0.3f ? colours::meterAmber : colours::screenText) });
        const float w = std::pow (10.0f, m.sideToMidDb / 10.0f);
        tiles.push_back ({ "WIDTH", juce::String (juce::roundToInt (100.0f * w / (1.0f + w))), "%", colours::screenText });
    }
    else
    {
        tiles.push_back ({ "MIX BUS", "--", {}, colours::screenDim });
    }

    if (report != nullptr)
    {
        tiles.push_back ({ "TRACKS", juce::String ((int) report->tracks.size()), {}, colours::screenText });
        tiles.push_back ({ "ENGINE", juce::String (report->lastTickMs, 2), "ms", colours::screenDim });
    }

    // One dark display across the top, like the screen on the hardware.
    auto area = getLocalBounds().toFloat();
    g.setColour (colours::screen);
    g.fillRoundedRectangle (area, 8.0f);
    area.reduce (6.0f, 0.0f);

    const float tileW = area.getWidth() / (float) juce::jmax<size_t> (8, tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& t = tiles[i];
        auto r = area.removeFromLeft (tileW);
        if (i > 0)
        {
            g.setColour (colours::screenLine);
            g.fillRect (r.getX(), r.getY() + 12.0f, 1.0f, r.getHeight() - 24.0f);
        }
        r.reduce (8.0f, 0.0f);
        g.setColour (colours::screenDim);
        g.setFont (font (11.0f, true));
        g.drawText (t.label, r.reduced (6.0f, 9.0f).removeFromTop (13.0f), juce::Justification::centredLeft);
        auto valueArea = r.reduced (6.0f, 8.0f).withTrimmedTop (16.0f);
        g.setColour (t.colour);
        g.setFont (monoFont (24.0f));
        const float vw = juce::GlyphArrangement::getStringWidth (monoFont (24.0f), t.value);
        g.drawText (t.value, valueArea, juce::Justification::centredLeft);
        g.setColour (colours::screenDim);
        g.setFont (font (12.0f));
        g.drawText (t.unit, valueArea.withTrimmedLeft (vw + 4.0f), juce::Justification::centredLeft, false);
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
    diagnostics.onExpandChanged = [this] (bool) { resized(); repaint(); };

    addChildComponent (chain);
    for (auto* b : { &adviceButton, &chainButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (5150);
        b->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b->setColour (juce::TextButton::buttonOnColourId, colours::amber);
        b->setColour (juce::TextButton::textColourOffId, colours::textDim);
        b->setColour (juce::TextButton::textColourOnId, colours::amberText);
        addAndMakeVisible (b);
    }
    adviceButton.setToggleState (true, juce::dontSendNotification);
    adviceButton.onClick = [this] { showChain (false); };
    chainButton.onClick = [this] { showChain (true); };
}

void MasterView::showChain (bool shouldShow)
{
    (shouldShow ? chainButton : adviceButton).setToggleState (true, juce::dontSendNotification);
    resized();
    repaint();
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
    if (current != nullptr && current->isActiveMaster && current->tracks.empty() && rack.isVisible() && ! isShowingChain())
    {
        // Only the mix bus is analysed: say what that covers and how to get more.
        auto note = rack.getBounds().toFloat().withTrimmedTop (24.0f).withTrimmedLeft ((float) ChannelStrip::kWidth + 12.0f).reduced (4.0f, 0.0f);
        if (note.getWidth() > 160.0f)
        {
            auto box = note.withHeight (juce::jmin (note.getHeight(), 190.0f));
            g.setColour (colours::panelRaised);
            g.fillRoundedRectangle (box, 10.0f);
            g.setColour (colours::amber);
            g.drawRoundedRectangle (box.reduced (1.0f), 10.0f, 2.0f);
            auto t = note.reduced (18.0f, 16.0f);
            drawWrapped (g, "Listening to the whole mix", font (18.0f, true), colours::text, t.removeFromTop (28.0f));
            drawWrapped (g, "From the mix bus alone you get loudness, headroom, stereo and overall tone advice.\n\n"
                            "To find out which instrument causes a problem, and which tracks clash with each other, "
                            "also put K MASTER on each track and leave its Mode on Listener. It detects "
                            "what each track is by itself.",
                         font (15.0f), colours::textDim, t);
        }
    }

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
    summary.setBounds (r.removeFromTop (66));
    r.removeFromTop (12);
    auto tabs = r.removeFromTop (30);
    adviceButton.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (6);
    chainButton.setBounds (tabs.removeFromLeft (140));
    r.removeFromTop (12);

    const bool showingChain = isShowingChain();
    chain.setVisible (showingChain);
    diagnostics.setVisible (! showingChain);
    if (showingChain)
    {
        rack.setVisible (false);
        chain.setBounds (r);
        return;
    }

    // Enlarged advice takes the whole area below the read-out.
    rack.setVisible (! diagnostics.isExpanded());
    if (! diagnostics.isExpanded())
    {
        const int rackWidth = juce::roundToInt ((float) r.getWidth() * 0.46f);
        rack.setBounds (r.removeFromLeft (rackWidth));
        r.removeFromLeft (16);
    }
    diagnostics.setBounds (r);
}

} // namespace aimix::ui
