#pragma once

#include "Theme.h"
#include "aimix/MixEngine.h"

#include <functional>
#include <map>
#include <optional>

namespace aimix::ui
{
// One issue: severity badge, category, title, affected tracks, why it was
// raised, the machine-readable action, and numbered step-by-step guidance.
class DiagnosticCard final : public juce::Component
{
public:
    explicit DiagnosticCard (const Suggestion& s);

    int getHeightForWidth (int width) const;
    const Suggestion& getSuggestion() const noexcept { return suggestion; }

    // Updates the card in place. Measured values in the text refresh at most
    // once a second so the numbers stay readable; severity and the
    // "looks fixed" state update immediately.
    void update (const Suggestion& s);

    std::function<void (const std::string&)> onDismiss;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Layout
    {
        juce::Rectangle<float> header, title, tracks, detail, action;
        std::vector<juce::Rectangle<float>> steps;
        float height = 0;
    };
    Layout computeLayout (float width) const;

    Suggestion suggestion;
    juce::uint32 lastTextUpdateMs = 0;
    juce::TextButton dismissButton { "Dismiss" };
};

class DiagnosticPanel final : public juce::Component
{
public:
    DiagnosticPanel();

    void setSuggestions (const std::vector<Suggestion>& list);
    void setTrackFilter (int64_t trackId);   // -1 = all tracks

    std::function<void (const std::string&)> onDismiss;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuild();
    void layoutCards();
    bool passesFilter (const Suggestion& s) const;

    std::vector<Suggestion> all;
    std::optional<Category> categoryFilter;
    int64_t trackFilter = -1;

    juce::OwnedArray<juce::TextButton> filterButtons;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<DiagnosticCard> cards;
    juce::Label emptyLabel;
};

} // namespace aimix::ui
