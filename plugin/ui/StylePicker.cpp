#include "StylePicker.h"

namespace aimix::ui
{
namespace
{
const char* eraBlurb (Era e)
{
    switch (e)
    {
        case Era::Modern:    return "Loud, tight, bright";
        case Era::OldSchool: return "Punchy, a bit dusty, more room";
        case Era::Vintage:   return "Warm tape and tubes, breathing";
    }
    return "";
}

void styleToggle (juce::TextButton& b)
{
    b.setClickingTogglesState (false);
    b.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    b.setColour (juce::TextButton::buttonOnColourId, colours::amber);
    b.setColour (juce::TextButton::textColourOffId, colours::textDim);
    b.setColour (juce::TextButton::textColourOnId, colours::amberText);
}
} // namespace

StylePicker::StylePicker()
{
    for (int g = 1; g < kNumGenres; ++g)
    {
        auto* b = genreButtons.add (new juce::TextButton (genreName ((Genre) g)));
        styleToggle (*b);
        b->onClick = [this, g] { style.genre = (Genre) g; refresh(); };
        addAndMakeVisible (b);
    }
    for (int e = 0; e < kNumEras; ++e)
    {
        auto* b = eraButtons.add (new juce::TextButton (eraName ((Era) e)));
        styleToggle (*b);
        b->onClick = [this, e] { style.era = (Era) e; refresh(); };
        addAndMakeVisible (b);
    }

    goButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    goButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    goButton.onClick = [this] { if (style.isSet() && onChosen) onChosen (style); };
    skipButton.setColour (juce::TextButton::buttonColourId, colours::panel);
    skipButton.setColour (juce::TextButton::textColourOffId, colours::textDim);
    skipButton.onClick = [this] { if (onSkip) onSkip(); };
    addAndMakeVisible (goButton);
    addAndMakeVisible (skipButton);
    refresh();
}

void StylePicker::setStyle (MixStyle s)
{
    style = s;
    refresh();
}

void StylePicker::refresh()
{
    for (int i = 0; i < genreButtons.size(); ++i)
        genreButtons[i]->setToggleState ((int) style.genre == i + 1, juce::dontSendNotification);
    for (int i = 0; i < eraButtons.size(); ++i)
        eraButtons[i]->setToggleState ((int) style.era == i, juce::dontSendNotification);
    goButton.setEnabled (style.isSet());
    goButton.setAlpha (style.isSet() ? 1.0f : 0.45f);
    repaint();
}

juce::Rectangle<int> StylePicker::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 760), juce::jmin (getHeight() - 40, 470));
}

void StylePicker::paint (juce::Graphics& g)
{
    g.fillAll (colours::background.withAlpha (0.92f));

    const auto c = card().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillRoundedRectangle (c.translated (0.0f, 4.0f), 14.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (c, 14.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (c, 14.0f, 1.0f);

    auto r = c.reduced (32.0f, 26.0f);
    g.setColour (colours::text);
    g.setFont (font (24.0f, true));
    g.drawText ("What are you mixing?", r.removeFromTop (34.0f), juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.fillRect (r.removeFromTop (3.0f).withWidth (64.0f));
    r.removeFromTop (8.0f);
    g.setColour (colours::textDim);
    g.setFont (font (14.0f));
    g.drawFittedText ("Pick the genre and style first. The targets for loudness, punch and tone, and the plugins and presets "
                      "suggested for each step, follow your choice.",
                      r.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 2);

    g.setColour (colours::textFaint);
    g.setFont (font (11.5f, true));
    g.drawText ("GENRE", genreButtons[0]->getBounds().toFloat().translated (0.0f, -20.0f).withHeight (16.0f), juce::Justification::centredLeft);
    g.drawText ("STYLE", eraButtons[0]->getBounds().toFloat().translated (0.0f, -20.0f).withHeight (16.0f), juce::Justification::centredLeft);

    g.setFont (font (12.0f));
    for (int i = 0; i < eraButtons.size(); ++i)
    {
        const auto b = eraButtons[i]->getBounds().toFloat();
        g.setColour (colours::textDim);
        g.drawText (eraBlurb ((Era) i), b.translated (0.0f, b.getHeight() + 2.0f).withHeight (18.0f), juce::Justification::centred);
    }

    // What the choice means, in numbers, on a small dark display.
    auto display = juce::Rectangle<float> (c.getX() + 32.0f, (float) goButton.getY() - 62.0f, c.getWidth() - 64.0f, 44.0f);
    g.setColour (colours::screen);
    g.fillRoundedRectangle (display, 6.0f);
    g.setFont (monoFont (13.5f));
    if (style.isSet())
    {
        const auto p = profileFor (style);
        g.setColour (colours::screenText);
        g.drawFittedText (juce::String (styleName (style)).toUpperCase() + "   " + p.summary
                              + "   |   target " + juce::String (p.targetLufs, 1) + " LUFS",
                          display.reduced (14.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    else
    {
        g.setColour (colours::screenDim);
        g.drawText ("CHOOSE A GENRE", display.reduced (14.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void StylePicker::resized()
{
    auto r = card().reduced (32, 26);
    r.removeFromTop (34 + 3 + 8 + 40 + 30);

    auto genres = r.removeFromTop (40);
    const int gw = (genres.getWidth() - (genreButtons.size() - 1) * 8) / genreButtons.size();
    for (auto* b : genreButtons)
    {
        b->setBounds (genres.removeFromLeft (gw));
        genres.removeFromLeft (8);
    }

    r.removeFromTop (34);
    auto eras = r.removeFromTop (40);
    const int ew = (eras.getWidth() - (eraButtons.size() - 1) * 8) / eraButtons.size();
    for (auto* b : eraButtons)
    {
        b->setBounds (eras.removeFromLeft (ew));
        eras.removeFromLeft (8);
    }

    auto bottom = r.removeFromBottom (44);
    goButton.setBounds (bottom.removeFromRight (200));
    bottom.removeFromRight (10);
    skipButton.setBounds (bottom.removeFromRight (110));
}

} // namespace aimix::ui
