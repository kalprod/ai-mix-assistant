#include "ChannelRack.h"

namespace aimix::ui
{
namespace
{
constexpr float kMeterMinDb = -60.0f;
constexpr float kMeterMaxDb = 6.0f;

float dbToY (float db, juce::Rectangle<float> r)
{
    const float n = juce::jlimit (0.0f, 1.0f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
    return r.getBottom() - n * r.getHeight();
}
}

//==============================================================================
void ChannelStrip::setData (const TrackSummary& summary, Issues i, bool isMasterStrip, bool selected)
{
    data = summary;
    issues = i;
    master = isMasterStrip;
    isSelected = selected;
    repaint();
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f, 0.0f);
    const auto& v = data.view;
    const float alpha = data.stale ? 0.4f : 1.0f;

    g.setColour ((master ? colours::panelRaised : colours::panel).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (isSelected ? colours::accent : colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, isSelected ? 2.0f : 1.0f);

    // role colour cap
    g.setColour (roleColour (v.role).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds.withHeight (4.0f), 2.0f);

    auto inner = bounds.reduced (8.0f, 0.0f);

    // name + role
    g.setColour (colours::text.withMultipliedAlpha (alpha));
    g.setFont (font (13.0f, true));
    g.drawFittedText (master ? juce::String ("MIX BUS") : juce::String (v.name), inner.withTrimmedTop (8).withHeight (18).withTrimmedRight (20).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (colours::textDim.withMultipliedAlpha (alpha));
    g.setFont (font (11.0f));
    const juce::String roleLine = data.stale         ? juce::String ("no signal")
                                : master             ? juce::String ("Mix bus")
                                : data.roleDetecting ? juce::String ("Auto: listening...")
                                : data.autoRole      ? roleText (v.role) + " (auto)"
                                                     : roleText (v.role);
    g.drawText (roleLine,
                inner.withTrimmedTop (26).withHeight (14), juce::Justification::centredLeft);

    // issue badge
    if (issues.count > 0)
    {
        auto badge = juce::Rectangle<float> (bounds.getRight() - 24.0f, 9.0f, 18.0f, 18.0f);
        g.setColour (severityColour (issues.worst));
        g.fillEllipse (badge);
        g.setColour (juce::Colours::white);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String (issues.count), badge, juce::Justification::centred);
    }

    const float h = bounds.getHeight();
    auto meterArea = juce::Rectangle<float> (inner.getX(), 50.0f, 34.0f, juce::jmax (40.0f, h - 50.0f - 136.0f));

    drawMeter (g, meterArea.withWidth (12.0f), v.rmsDb[0], v.peakDb[0]);
    drawMeter (g, meterArea.withX (meterArea.getX() + 15.0f).withWidth (12.0f), v.rmsDb[1], v.peakDb[1]);

    // LUFS readouts beside the meters
    auto readout = juce::Rectangle<float> (meterArea.getRight() + 4.0f, meterArea.getY(), inner.getRight() - meterArea.getRight() - 4.0f, 30.0f);
    auto drawStat = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        g.setColour (colours::textFaint.withMultipliedAlpha (alpha));
        g.setFont (font (9.5f));
        g.drawText (label, readout.withHeight (11), juce::Justification::centredRight);
        g.setColour (c.withMultipliedAlpha (alpha));
        g.setFont (monoFont (12.5f));
        g.drawText (value, readout.withTrimmedTop (11).withHeight (16), juce::Justification::centredRight);
        readout.translate (0, 32.0f);
    };
    drawStat ("LUFS M", formatLufs (v.momentaryLufs), colours::text);
    drawStat ("LUFS S", formatLufs (v.shortTermLufs), colours::text);
    drawStat ("LUFS I", formatLufs (v.integratedLufs), colours::textDim);
    const float peak = v.maxPeakDb();
    drawStat ("PEAK", formatDb (peak), peak >= -0.3f ? colours::meterRed : colours::text);

    // spectrum
    auto spec = juce::Rectangle<float> (inner.getX(), h - 128.0f, inner.getWidth(), 46.0f);
    drawSpectrum (g, spec);

    // phase correlation
    auto corrLabel = juce::Rectangle<float> (inner.getX(), h - 76.0f, inner.getWidth(), 13.0f);
    g.setFont (font (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("PHASE", corrLabel, juce::Justification::centredLeft);
    g.setColour (v.correlation < 0.0f ? colours::meterRed : colours::textDim);
    g.setFont (monoFont (11.0f));
    g.drawText (juce::String (v.correlation, 2), corrLabel, juce::Justification::centredRight);
    drawCorrelation (g, juce::Rectangle<float> (inner.getX(), h - 61.0f, inner.getWidth(), 7.0f));

    // stereo width
    const float width = std::pow (10.0f, v.sideToMidDb / 10.0f);
    const float widthPct = 100.0f * width / (1.0f + width);
    auto widthRow = juce::Rectangle<float> (inner.getX(), h - 44.0f, inner.getWidth(), 13.0f);
    g.setFont (font (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("WIDTH", widthRow, juce::Justification::centredLeft);
    g.setFont (monoFont (11.0f));
    g.setColour (colours::textDim);
    g.drawText (v.numChannels < 2 ? juce::String ("mono") : juce::String (juce::roundToInt (widthPct)) + "%", widthRow, juce::Justification::centredRight);

    auto widthBar = juce::Rectangle<float> (inner.getX(), h - 28.0f, inner.getWidth(), 5.0f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (widthBar, 2.0f);
    g.setColour (colours::accent.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (widthBar.withWidth (widthBar.getWidth() * juce::jlimit (0.0f, 1.0f, widthPct / 100.0f)), 2.0f);
}

void ChannelStrip::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float rmsDb, float peakDb) const
{
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 2.0f);

    const float yRms = dbToY (rmsDb, r);
    const float yZero = dbToY (0.0f, r);
    const float yAmber = dbToY (-12.0f, r);

    juce::ColourGradient grad (colours::meterRed, r.getX(), yZero, colours::meterGreen, r.getX(), r.getBottom(), false);
    grad.addColour ((yAmber - yZero) / (r.getBottom() - yZero) * 0.99, colours::meterAmber);
    g.setGradientFill (grad);
    g.fillRect (r.withTop (yRms).reduced (1.0f, 0.0f));

    const float yPeak = dbToY (peakDb, r);
    g.setColour (peakDb >= -0.3f ? colours::meterRed : colours::text);
    g.fillRect (r.getX() + 1.0f, yPeak - 1.0f, r.getWidth() - 2.0f, 2.0f);

    g.setColour (colours::outline);
    for (float db : { 0.0f, -12.0f, -24.0f, -48.0f })
        g.fillRect (r.getRight() + 1.0f, dbToY (db, r), 2.0f, 1.0f);
}

void ChannelStrip::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r) const
{
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 3.0f);

    const auto& s = data.displaySpectrumDb;
    juce::Path line;
    for (int i = 0; i < kDisplaySpectrumPoints; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (kDisplaySpectrumPoints - 1);
        const float n = juce::jlimit (0.0f, 1.0f, (s[(size_t) i] + 90.0f) / 84.0f);
        const float y = r.getBottom() - 2.0f - n * (r.getHeight() - 4.0f);
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    juce::Path fill (line);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();

    const auto c = roleColour (data.view.role).withMultipliedAlpha (data.stale ? 0.4f : 1.0f);
    g.setColour (c.withAlpha (0.25f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (line, juce::PathStrokeType (1.2f));
}

void ChannelStrip::drawCorrelation (juce::Graphics& g, juce::Rectangle<float> r) const
{
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 2.0f);
    const float cx = r.getCentreX();
    g.setColour (colours::outline);
    g.fillRect (cx - 0.5f, r.getY() - 1.0f, 1.0f, r.getHeight() + 2.0f);

    const float c = juce::jlimit (-1.0f, 1.0f, data.view.correlation);
    const float x = cx + c * r.getWidth() * 0.5f;
    g.setColour (c < 0.0f ? colours::meterRed : (c < 0.3f ? colours::meterAmber : colours::meterGreen));
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, x), r.getY() + 1.0f, std::abs (x - cx), r.getHeight() - 2.0f), 1.5f);
    g.fillEllipse (x - 3.5f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
ChannelRack::ChannelRack()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void ChannelRack::paint (juce::Graphics& g)
{
    g.setColour (colours::textDim);
    g.setFont (font (12.0f, true));
    g.drawText ("CHANNEL RACK", getLocalBounds().removeFromTop (22).withTrimmedLeft (4), juce::Justification::centredLeft);
}

void ChannelRack::setReport (const MixReport& report)
{
    std::map<uint32_t, ChannelStrip::Issues> issues;
    for (const auto& s : report.suggestions)
        for (auto id : s.trackIds)
        {
            auto& i = issues[id];
            if (i.count == 0 || s.severity > i.worst) i.worst = s.severity;
            i.count++;
        }

    const int needed = (int) report.tracks.size() + (report.hasMaster ? 1 : 0);
    while (strips.size() < needed)
        content.addAndMakeVisible (strips.add (new ChannelStrip()));
    while (strips.size() > needed)
        strips.removeLast();

    int index = 0;
    auto bind = [&] (const TrackSummary& t, bool isMaster)
    {
        auto* strip = strips[index++];
        const auto id = (int64_t) t.view.trackId;
        strip->setData (t, issues[t.view.trackId], isMaster, id == selectedTrack);
        strip->onClick = [this, id]
        {
            selectedTrack = selectedTrack == id ? -1 : id;
            if (onSelectionChanged) onSelectionChanged (selectedTrack);
        };
    };
    if (report.hasMaster)
        bind (report.master, true);
    for (const auto& t : report.tracks)
        bind (t, false);

    layoutStrips();
}

void ChannelRack::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (24));
    layoutStrips();
}

void ChannelRack::layoutStrips()
{
    const int h = juce::jmax (300, viewport.getHeight() - viewport.getScrollBarThickness() - 2);
    content.setSize (juce::jmax (1, strips.size() * ChannelStrip::kWidth), h);
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::kWidth, 0, ChannelStrip::kWidth, h);
}

} // namespace aimix::ui
