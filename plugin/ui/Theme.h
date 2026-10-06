#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "aimix/AnalysisPayload.h"
#include "aimix/Suggestion.h"

namespace aimix::ui
{
namespace colours
{
    const juce::Colour background   { 0xff121418 };
    const juce::Colour panel        { 0xff1a1d23 };
    const juce::Colour panelRaised  { 0xff22262e };
    const juce::Colour outline      { 0xff2e333d };
    const juce::Colour text         { 0xffe7e9ee };
    const juce::Colour textDim      { 0xff9aa1ad };
    const juce::Colour textFaint    { 0xff666d79 };
    const juce::Colour accent       { 0xff5b9cff };
    const juce::Colour meterGreen   { 0xff3ccf7a };
    const juce::Colour meterAmber   { 0xffe8b33a };
    const juce::Colour meterRed     { 0xffef5350 };
    const juce::Colour critical     { 0xffef5350 };
    const juce::Colour warning      { 0xfff0a530 };
    const juce::Colour info         { 0xff4f8ff7 };
}

inline juce::Colour severityColour (Severity s)
{
    switch (s)
    {
        case Severity::Critical: return colours::critical;
        case Severity::Warning:  return colours::warning;
        case Severity::Info:     return colours::info;
    }
    return colours::info;
}

inline juce::String severityLabel (Severity s)
{
    switch (s)
    {
        case Severity::Critical: return "CRITICAL";
        case Severity::Warning:  return "WARNING";
        case Severity::Info:     return "INFO";
    }
    return "INFO";
}

// "Unknown" is shown as "Instrument": the detector uses it for anything that
// isn't drums, bass or a vocal.
inline juce::String roleText (TrackRole r)
{
    return r == TrackRole::Unknown ? juce::String ("Instrument") : juce::String (toString (r));
}

inline juce::String categoryLabel (Category c)
{
    switch (c)
    {
        case Category::Gain:    return "GAIN";
        case Category::Eq:      return "EQ";
        case Category::Panning: return "PANNING";
        case Category::Phase:   return "PHASE";
    }
    return {};
}

inline juce::Colour roleColour (TrackRole r)
{
    switch (r)
    {
        case TrackRole::Vocal:     return juce::Colour (0xffe57399);
        case TrackRole::Kick:      return juce::Colour (0xffef7d4f);
        case TrackRole::Snare:     return juce::Colour (0xfff2a65a);
        case TrackRole::Drums:     return juce::Colour (0xffe8c25a);
        case TrackRole::Bass:      return juce::Colour (0xffa87ef0);
        case TrackRole::Guitar:    return juce::Colour (0xff6cc787);
        case TrackRole::Keys:      return juce::Colour (0xff4fc4cf);
        case TrackRole::Synth:     return juce::Colour (0xff5b9cff);
        case TrackRole::Fx:        return juce::Colour (0xff9aa1ad);
        case TrackRole::MasterBus: return juce::Colour (0xffe7e9ee);
        case TrackRole::Unknown:
        case TrackRole::NumRoles:  break;
    }
    return juce::Colour (0xff7d8594);
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

// Height of wrapped text at a given width.
inline float textHeight (const juce::String& text, const juce::Font& f, float width)
{
    juce::AttributedString s;
    s.append (text, f, colours::text);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, width);
    return std::ceil (layout.getHeight());
}

inline void drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Colour c, juce::Rectangle<float> area)
{
    juce::AttributedString s;
    s.append (text, f, c);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, area.getWidth());
    layout.draw (g, area);
}

inline juce::String formatDb (float db, int decimals = 1)
{
    return db <= -119.0f ? juce::String ("-inf") : juce::String (db, decimals);
}

inline juce::String formatLufs (float lufs)
{
    return lufs <= -70.0f ? juce::String ("--") : juce::String (lufs, 1);
}

} // namespace aimix::ui
