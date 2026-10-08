#include "ChainPanel.h"

namespace aimix::ui
{
namespace
{
constexpr float kPad = 16.0f;
constexpr float kNumberW = 46.0f;
constexpr float kRowGap = 10.0f;
constexpr float kChipH = 62.0f;

juce::String missingText (const std::string& slot)
{
    if (slot == "cleanup_eq" || slot == "character_eq" || slot == "program_eq")
        return "None of your plugins fits this. Your DAW's own EQ works fine here.";
    if (slot == "compressor" || slot == "compressor2" || slot == "bus_comp")
        return "None of your plugins fits this. Your DAW's own compressor works fine here.";
    if (slot == "limiter")
        return "None of your plugins fits this. Your DAW's own limiter works fine here.";
    return "Nothing suitable found. You can skip this one.";
}

juce::String characterBadge (const std::string& c)
{
    if (c == "analog")          return "ANALOG";
    if (c == "analog_inspired") return "ANALOG-STYLE";
    return "CLEAN DIGITAL";
}

float leftColumnWidth (float width) { return juce::jlimit (180.0f, 320.0f, width * 0.30f); }
float rightColumnWidth (float width) { return width - 28.0f - leftColumnWidth (width); }

constexpr float kCaptionH = 16.0f;
constexpr float kWellH = 40.0f;
constexpr float kPresetBadgeW = 112.0f;

juce::String presetBadge (PresetMatch m)
{
    switch (m)
    {
        case PresetMatch::Exact:    return "PRESET FOUND";
        case PresetMatch::Closest:  return "CLOSEST PRESET";
        case PresetMatch::NotFound: return "NO PRESET FILE";
    }
    return {};
}

juce::Colour presetColour (PresetMatch m)
{
    switch (m)
    {
        case PresetMatch::Exact:    return colours::meterGreen.darker (0.15f);
        case PresetMatch::Closest:  return colours::amber;
        case PresetMatch::NotFound: return colours::outline;
    }
    return colours::outline;
}

juce::String presetLine (const PresetAdvice& p)
{
    return juce::String (p.text.empty() ? std::string ("Start from its default setting.") : p.text);
}

// Badge + wrapped text, used for option 1.
float presetHeight (const PresetAdvice& p, float w)
{
    return juce::jmax (20.0f, textHeight (presetLine (p), font (13.5f), w - kPresetBadgeW - 10.0f));
}

// Options 2 and 3: a small card with the plugin, its preset and how it differs.
float alternativeHeight (const ChainOption& o, float w)
{
    const float tw = w - 24.0f;
    return 10.0f + 20.0f + 4.0f + textHeight ("Preset: " + presetLine (o.preset), font (12.5f), tw)
         + 3.0f + textHeight (juce::String (o.note), font (12.5f, true), tw) + 10.0f;
}

void drawCharacterBadge (juce::Graphics& g, const std::string& character, juce::Rectangle<float> area, bool onScreen)
{
    const bool analog = character == "analog";
    const bool inspired = character == "analog_inspired";
    g.setColour (analog ? colours::amber : inspired ? colours::amber.withAlpha (0.55f)
                                         : (onScreen ? colours::screenLine : colours::outline.withAlpha (0.5f)));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (analog || inspired ? colours::amberText : (onScreen ? colours::screenText : colours::textDim));
    g.setFont (font (area.getHeight() > 18.0f ? 11.0f : 10.0f, true));
    g.drawText (characterBadge (character), area, juce::Justification::centred);
}

float badgeWidth (const std::string& character, float fontH)
{
    return juce::GlyphArrangement::getStringWidth (font (fontH, true), characterBadge (character)) + 16.0f;
}
}

ChainPanel::ChainPanel()
{
    rescanButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    rescanButton.setColour (juce::TextButton::textColourOffId, colours::textDim);
    rescanButton.onClick = [this] { if (onRescan) onRescan(); };
    addAndMakeVisible (rescanButton);

    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void ChainPanel::setLibrary (std::vector<PluginInfo> plugins, const juce::String& statusText, bool isScanning)
{
    library = std::move (plugins);
    status = statusText;
    scanning = isScanning;
    rescanButton.setEnabled (! scanning);
    recompute();
    repaint();
}

void ChainPanel::setContext (const ChainContext& ctx)
{
    hasContext = true;
    context = ctx;
    recompute();
}

void ChainPanel::recompute()
{
    auto next = recommendChain (library, context);

    // Repaint only when something the user reads has changed.
    auto same = [] (const ChainRecommendation& a, const ChainRecommendation& b)
    {
        if (a.role != b.role || a.master != b.master || a.slots.size() != b.slots.size())
            return false;
        for (size_t i = 0; i < a.slots.size(); ++i)
        {
            const auto& x = a.slots[i];
            const auto& y = b.slots[i];
            if (x.pickName != y.pickName || x.preset != y.preset || x.options.size() != y.options.size()
                || x.knobs.size() != y.knobs.size())
                return false;
            for (size_t o = 0; o < x.options.size(); ++o)
                if (x.options[o].name != y.options[o].name || x.options[o].preset.text != y.options[o].preset.text
                    || x.options[o].note != y.options[o].note)
                    return false;
            for (size_t k = 0; k < x.knobs.size(); ++k)
                if (x.knobs[k].setting != y.knobs[k].setting)
                    return false;
        }
        return true;
    };
    if (same (next, rec) && ! rec.slots.empty() && styleShown == context.style)
        return;
    rec = std::move (next);
    styleShown = context.style;
    resized();
    repaint();
    rows.repaint();
}

juce::String ChainPanel::headline() const
{
    juce::String h = rec.master ? juce::String ("Suggested mix bus chain")
                                : "Suggested chain for " + roleText (rec.role).toLowerCase();
    if (context.style.isSet())
        h << "  |  " << juce::String (styleName (context.style));
    return h;
}

void ChainPanel::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (kHeaderHeight).toFloat();
    g.setColour (colours::text);
    g.setFont (font (19.0f, true));
    const auto title = headline();
    const float titleW = juce::GlyphArrangement::getStringWidth (font (19.0f, true), title);
    g.drawText (title, header.withWidth (titleW + 4.0f).withTrimmedBottom (14.0f), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (font (13.0f));
    g.drawText (daw.howTo.isNotEmpty() ? daw.howTo
                                       : juce::String ("From the plugins on this computer, analog models first. Top to bottom is the order to insert them."),
                header.withTrimmedTop (24.0f).withTrimmedRight ((float) rescanButton.getWidth() + 230.0f),
                juce::Justification::centredLeft, true);

    auto statusArea = header.withTrimmedRight ((float) rescanButton.getWidth() + 12.0f).removeFromRight (220.0f).withTrimmedBottom (14.0f);
    g.setColour (scanning ? colours::warning : colours::textFaint);
    g.setFont (font (12.5f, true));
    g.drawText (status, statusArea, juce::Justification::centredRight, true);

    if (library.empty() && ! scanning)
    {
        auto body = getLocalBounds().withTrimmedTop (kHeaderHeight + 8).toFloat();
        drawWrapped (g, "No plugins found yet. Press Rescan plugins to look again. Audio Units and VST3 plugins in the "
                        "standard folders are found automatically.",
                     font (15.0f), colours::textDim, body.reduced (4.0f).withHeight (60.0f));
    }
}

void ChainPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderHeight);
    rescanButton.setBounds (header.removeFromRight (130).withSizeKeepingCentre (130, 28).withY (header.getY() + 2));
    r.removeFromTop (6);
    viewport.setBounds (r);
    const int w = r.getWidth() - viewport.getScrollBarThickness() - 2;
    rows.setSize (w, rows.heightFor (w));
}

//==============================================================================
ChainPanel::Daw ChainPanel::dawFor (const juce::String& host)
{
    Daw d;
    if (host.containsIgnoreCase ("FL Studio") || host.containsIgnoreCase ("Fruity"))
        d = { "Mixer insert slot", "In the Mixer (F9), load these into this track's insert slots from the top down. Keep K MASTER in the last slot." };
    else if (host.containsIgnoreCase ("Studio One"))
        d = { "Insert", "Open this channel's Inserts in the Console (F3) and add these in order. Keep K MASTER at the bottom." };
    else if (host.containsIgnoreCase ("Logic") || host.containsIgnoreCase ("GarageBand"))
        d = { "Audio FX slot", "Click the empty Audio FX slots on this channel strip from the top down. Keep K MASTER in the lowest slot." };
    else
        d = { "Insert slot", "Add these to this track's insert slots from the top down. Keep K MASTER last." };
    return d;
}

juce::Rectangle<float> ChainPanel::Rows::tickBox (size_t index) const
{
    float y = 0.0f;
    const float width = (float) getWidth();
    for (size_t i = 0; i < owner.rec.slots.size(); ++i)
    {
        const float h = rowHeight (owner.rec.slots[i], width);
        if (i == index)
            return { 14.0f + 5.0f, y + 14.0f + 44.0f, 20.0f, 20.0f };   // under the number
        y += h + kRowGap;
    }
    return {};
}

void ChainPanel::Rows::mouseMove (const juce::MouseEvent& e)
{
    bool over = false;
    for (size_t i = 0; i < owner.rec.slots.size(); ++i)
        over |= owner.rec.slots[i].pick >= 0 && tickBox (i).expanded (6.0f).contains (e.position);
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ChainPanel::Rows::mouseUp (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < owner.rec.slots.size(); ++i)
    {
        const auto& s = owner.rec.slots[i];
        if (s.pick < 0 || ! tickBox (i).expanded (6.0f).contains (e.position))
            continue;
        const auto key = addedKey (s);
        // One tick per slot: drop any older tick for a different plugin.
        for (int k = owner.addedKeys.size(); --k >= 0;)
            if (owner.addedKeys[k].startsWith (juce::String (s.id) + "=") && owner.addedKeys[k] != key)
                owner.addedKeys.remove (k);
        if (owner.addedKeys.contains (key))
            owner.addedKeys.removeString (key);
        else
            owner.addedKeys.add (key);
        repaint();
        if (owner.onAddedChanged)
            owner.onAddedChanged (owner.addedKeys);
        return;
    }
}

float ChainPanel::Rows::rowHeight (const ChainSlot& s, float width) const
{
    const float leftW = leftColumnWidth (width) - kNumberW;
    const float left = juce::jmax (70.0f, 42.0f + textHeight (juce::String (s.why), font (13.5f), leftW - 12.0f));
    const float rightW = rightColumnWidth (width);
    float right = kWellH;
    if (s.pick >= 0 && ! s.options.empty())
    {
        right += kCaptionH + 4.0f;
        right += 8.0f + presetHeight (s.options[0].preset, rightW);
        right += 8.0f + kChipH;
        for (size_t o = 1; o < s.options.size(); ++o)
            right += 8.0f + alternativeHeight (s.options[o], rightW);
    }
    return juce::jmax (left, right) + 2.0f * 14.0f;
}

int ChainPanel::Rows::heightFor (int width) const
{
    float h = 0.0f;
    for (const auto& s : owner.rec.slots)
        h += rowHeight (s, (float) width) + kRowGap;
    return (int) std::ceil (h);
}

void ChainPanel::Rows::paint (juce::Graphics& g)
{
    const float width = (float) getWidth();
    float y = 0.0f;
    int number = 1;

    for (const auto& s : owner.rec.slots)
    {
        const float h = rowHeight (s, width);
        auto card = juce::Rectangle<float> (0.0f, y, width, h);
        y += h + kRowGap;

        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        auto inner = card.reduced (14.0f, 14.0f);

        // Numbered pad: red when it should go in, outlined when optional.
        auto num = inner.removeFromLeft (kNumberW).removeFromTop (30.0f).withWidth (30.0f);
        if (s.optional)
        {
            g.setColour (colours::outline);
            g.drawEllipse (num.reduced (1.0f), 2.0f);
            g.setColour (colours::textDim);
        }
        else
        {
            g.setColour (colours::accent);
            g.fillEllipse (num);
            g.setColour (colours::panelRaised);
        }
        g.setFont (font (15.0f, true));
        g.drawText (juce::String (number++), num, juce::Justification::centred);

        auto left = inner.removeFromLeft (leftColumnWidth (width) - kNumberW);
        g.setColour (colours::text);
        g.setFont (font (17.0f, true));
        g.drawText (juce::String (s.label), left.removeFromTop (22.0f), juce::Justification::centredLeft, true);
        if (s.optional)
        {
            g.setColour (colours::textFaint);
            g.setFont (font (11.0f, true));
            const float lw = juce::GlyphArrangement::getStringWidth (font (17.0f, true), juce::String (s.label));
            g.drawText ("OPTIONAL", juce::Rectangle<float> (left.getX() + lw + 10.0f, left.getY() - 22.0f, 80.0f, 22.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (colours::accent);
        g.setFont (font (11.0f, true));
        g.drawText (owner.daw.slotWord.toUpperCase() + " " + juce::String (number - 1), left.removeFromTop (16.0f),
                    juce::Justification::centredLeft, true);
        drawWrapped (g, juce::String (s.why), font (13.5f), colours::textDim, left.withTrimmedTop (2.0f).withTrimmedRight (12.0f));

        // Tick box: "I've added this one".
        if (s.pick >= 0)
        {
            const bool added = owner.addedKeys.contains (addedKey (s));
            auto box = tickBox ((size_t) (number - 2));
            g.setColour (added ? colours::meterGreen : colours::panelRaised);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (added ? colours::meterGreen.darker (0.3f) : colours::outline);
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.5f);
            if (added)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY());
                tick.lineTo (box.getX() + 8.5f, box.getBottom() - 5.0f);
                tick.lineTo (box.getRight() - 4.0f, box.getY() + 5.0f);
                g.setColour (juce::Colours::white);
                g.strokePath (tick, juce::PathStrokeType (2.2f));
            }
            g.setColour (added ? colours::meterGreen.darker (0.4f) : colours::textFaint);
            g.setFont (font (9.5f, true));
            g.drawText (added ? "ADDED" : "ADD", box.translated (-12.0f, 21.0f).withWidth (44.0f).withHeight (12.0f),
                        juce::Justification::centred);
        }

        // Which plugin: a dark display with the name and an analog badge.
        auto right = inner;
        if (s.pick < 0 || s.options.empty())
        {
            auto well = right.removeFromTop (kWellH);
            g.setColour (colours::screen);
            g.fillRoundedRectangle (well, 7.0f);
            g.setColour (colours::screenDim);
            g.setFont (font (14.0f));
            g.drawText (missingText (s.id), well.reduced (14.0f, 0.0f), juce::Justification::centredLeft, true);
            continue;
        }

        const auto& best = s.options[0];
        const float rightW = right.getWidth();
        g.setColour (colours::accent);
        g.setFont (font (11.0f, true));
        g.drawText (owner.context.style.isSet() ? "OPTION 1  |  BEST PICK FOR " + juce::String (styleName (owner.context.style)).toUpperCase()
                                                : juce::String ("OPTION 1  |  BEST PICK"),
                    right.removeFromTop (kCaptionH), juce::Justification::centredLeft, true);
        right.removeFromTop (4.0f);

        auto well = right.removeFromTop (kWellH);
        g.setColour (colours::screen);
        g.fillRoundedRectangle (well, 7.0f);
        auto wellText = well.reduced (14.0f, 0.0f);
        const float bw = badgeWidth (best.character, 11.0f);
        drawCharacterBadge (g, best.character, wellText.removeFromRight (bw).withSizeKeepingCentre (bw, 20.0f), true);

        juce::String name (best.name);
        if (! best.family.empty() && ! name.containsIgnoreCase (best.family))
            name << "   " << juce::String (best.family) << " style";
        g.setColour (colours::screenText);
        g.setFont (monoFont (16.0f));
        g.drawFittedText (name, wellText.withTrimmedRight (10.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

        // The preset: found, closest (with how to finish it), or none saved.
        right.removeFromTop (8.0f);
        auto presetArea = right.removeFromTop (presetHeight (best.preset, rightW));
        auto pb = presetArea.removeFromLeft (kPresetBadgeW).removeFromTop (20.0f);
        g.setColour (presetColour (best.preset.match));
        g.fillRoundedRectangle (pb, 4.0f);
        g.setColour (best.preset.match == PresetMatch::Exact ? juce::Colours::white
                     : best.preset.match == PresetMatch::Closest ? colours::amberText : colours::textDim);
        g.setFont (font (10.0f, true));
        g.drawText (presetBadge (best.preset.match), pb, juce::Justification::centred);
        drawWrapped (g, presetLine (best.preset), font (13.5f), colours::text, presetArea.withTrimmedLeft (10.0f).translated (0.0f, 1.0f));

        // Quick-tweak knobs: the 2-4 controls to set first.
        right.removeFromTop (8.0f);
        auto chips = right.removeFromTop (kChipH);
        const int n = (int) s.knobs.size();
        const float chipW = (chips.getWidth() - 8.0f * (float) (n - 1)) / (float) juce::jmax (1, n);
        for (int k = 0; k < n; ++k)
        {
            auto chip = chips.removeFromLeft (chipW);
            chips.removeFromLeft (8.0f);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);
            auto ct = chip.reduced (10.0f, 6.0f);
            g.setColour (colours::accent);
            g.setFont (font (10.5f, true));
            g.drawText (juce::String (s.knobs[(size_t) k].knob).toUpperCase(), ct.removeFromTop (13.0f), juce::Justification::centredLeft, true);
            g.setColour (colours::text);
            g.setFont (font (12.5f));
            g.drawFittedText (juce::String (s.knobs[(size_t) k].setting), ct.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
        }

        // Options 2 and 3 from the user's own plugins, and how they differ.
        for (size_t o = 1; o < s.options.size(); ++o)
        {
            const auto& alt = s.options[o];
            right.removeFromTop (8.0f);
            auto box = right.removeFromTop (alternativeHeight (alt, rightW));
            g.setColour (colours::panel);
            g.fillRoundedRectangle (box, 7.0f);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);

            auto t = box.reduced (12.0f, 10.0f);
            auto line = t.removeFromTop (20.0f);
            g.setColour (colours::accent);
            g.setFont (font (11.0f, true));
            g.drawText ("OPTION " + juce::String ((int) o + 1), line.removeFromLeft (70.0f), juce::Justification::centredLeft);
            const float abw = badgeWidth (alt.character, 10.0f);
            drawCharacterBadge (g, alt.character, line.removeFromRight (abw).withSizeKeepingCentre (abw, 17.0f), false);
            g.setColour (colours::text);
            g.setFont (font (14.5f, true));
            g.drawFittedText (juce::String (alt.name), line.withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

            t.removeFromTop (4.0f);
            const auto presetText = "Preset: " + presetLine (alt.preset);
            const float ph = textHeight (presetText, font (12.5f), t.getWidth());
            drawWrapped (g, presetText, font (12.5f), colours::textDim, t.removeFromTop (ph));
            t.removeFromTop (3.0f);
            drawWrapped (g, juce::String (alt.note), font (12.5f, true), colours::text, t);
        }
    }
}

} // namespace aimix::ui
