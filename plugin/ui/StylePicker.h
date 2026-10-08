#pragma once

#include "Theme.h"
#include "aimix/MixStyle.h"

#include <functional>

namespace aimix::ui
{
// "What are you mixing?" Shown before the analysis when no genre is chosen,
// and again from the header's genre button. The choice sets the loudness,
// punch and tone targets and steers the plugin and preset picks.
class StylePicker final : public juce::Component
{
public:
    StylePicker();

    void setStyle (MixStyle s);
    MixStyle getStyle() const noexcept { return style; }

    std::function<void (MixStyle)> onChosen;   // "Analyse my mix"
    std::function<void()> onSkip;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    juce::Rectangle<int> card() const;

    MixStyle style;
    juce::OwnedArray<juce::TextButton> genreButtons, eraButtons;
    juce::TextButton goButton { "Analyse my mix" }, skipButton { "Not now" };
};

} // namespace aimix::ui
