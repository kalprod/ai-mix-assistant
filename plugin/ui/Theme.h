#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "aimix/AnalysisPayload.h"
#include "aimix/Suggestion.h"

namespace aimix::ui
{
// Retro hardware look: warm cream body, dark display panels for meters and
// read-outs, red and amber accents, and pad-style coloured outlines per role.
namespace colours
{
    // body
    const juce::Colour background   { 0xffd8d3c8 };   // warm grey chassis
    const juce::Colour panel        { 0xffe7e3d9 };   // raised cream panel
    const juce::Colour panelRaised  { 0xfff3f0ea };   // lightest surface (cards)
    const juce::Colour outline      { 0xffb5ae9f };
    const juce::Colour text         { 0xff1d1d1f };
    const juce::Colour textDim      { 0xff4a4640 };
    const juce::Colour textFaint    { 0xff7f786d };
    const juce::Colour accent       { 0xffd42a2a };   // signature red
    const juce::Colour amber        { 0xfff2a21a };   // backlit button amber
    const juce::Colour amberText    { 0xff3a2600 };

    // dark display (meters, spectra, read-outs)
    const juce::Colour screen       { 0xff18191d };
    const juce::Colour screenLine   { 0xff34363e };
    const juce::Colour screenText   { 0xffece8e0 };
    const juce::Colour screenDim    { 0xff9a968e };

    const juce::Colour meterGreen   { 0xff38c96f };
    const juce::Colour meterAmber   { 0xfff2a21a };
    const juce::Colour meterRed     { 0xffe5383b };
    const juce::Colour critical     { 0xffd42a2a };
    const juce::Colour warning      { 0xffe08a00 };
    const juce::Colour info         { 0xff2f6fd6 };
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
        case Category::Dynamics: return "DYNAMICS";
        case Category::Depth:   return "DEPTH";
        case Category::General: return "TIP";
    }
    return {};
}

// Pad-style outline colour per role.
inline juce::Colour roleColour (TrackRole r)
{
    switch (r)
    {
        case TrackRole::Vocal:     return juce::Colour (0xffd63aa0);   // magenta
        case TrackRole::Kick:      return juce::Colour (0xffe8502a);   // orange-red
        case TrackRole::Snare:     return juce::Colour (0xfff0a020);   // amber
        case TrackRole::Drums:     return juce::Colour (0xffd9b81c);   // yellow
        case TrackRole::Bass:      return juce::Colour (0xff7d55dc);   // violet
        case TrackRole::Guitar:    return juce::Colour (0xff35b45a);   // green
        case TrackRole::Keys:      return juce::Colour (0xff22b3c8);   // cyan
        case TrackRole::Synth:     return juce::Colour (0xff3d7be0);   // blue
        case TrackRole::Fx:        return juce::Colour (0xff8c877e);
        case TrackRole::MasterBus: return juce::Colour (0xff1d1d1f);
        case TrackRole::Unknown:
        case TrackRole::NumRoles:  break;
    }
    return juce::Colour (0xff22b3c8);
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

// Light colour scheme for the stock widgets (menus, combo boxes, scrollbars,
// buttons) so pop-ups match the cream body instead of JUCE's dark default.
class RetroLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              colours::background,   // windowBackground
              colours::panel,        // widgetBackground
              colours::panelRaised,  // menuBackground
              colours::outline,      // outline
              colours::text,         // defaultText
              colours::panel,        // defaultFill
              colours::amberText,    // highlightedText
              colours::amber,        // highlightedFill
              colours::text })       // menuText
    {
        setColour (juce::ComboBox::backgroundColourId, colours::panelRaised);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::accent);
        setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, colours::amber);
        setColour (juce::TextButton::textColourOffId, colours::textDim);
        setColour (juce::TextButton::textColourOnId, colours::amberText);
        setColour (juce::ScrollBar::thumbColourId, colours::outline);
        setColour (juce::TextEditor::backgroundColourId, colours::panelRaised);
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::CaretComponent::caretColourId, colours::accent);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::amber);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::amberText);
    }
};

} // namespace aimix::ui
