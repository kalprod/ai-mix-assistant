#include "DiagnosticCards.h"

namespace aimix::ui
{
namespace
{
constexpr float kPad = 18.0f;
constexpr float kStepIndent = 32.0f;

// Sized to be read at arm's length while mixing.
const juce::Font titleFont()  { return font (19.0f, true); }
const juce::Font bodyFont()   { return font (15.5f); }
const juce::Font smallFont()  { return font (13.0f); }

juce::String trackLine (const Suggestion& s)
{
    juce::StringArray names;
    for (auto& n : s.trackNames)
        names.add (n);
    return names.joinIntoString ("  \xc2\xb7  ");   // middle dot
}
}

//==============================================================================
DiagnosticCard::DiagnosticCard (const Suggestion& s) : suggestion (s)
{
    dismissButton.setColour (juce::TextButton::buttonColourId, colours::panel);
    dismissButton.setColour (juce::TextButton::textColourOffId, colours::textDim);
    dismissButton.onClick = [this] { if (onDismiss) onDismiss (suggestion.key); };
    addAndMakeVisible (dismissButton);
    lastTextUpdateMs = juce::Time::getMillisecondCounter();
}

void DiagnosticCard::update (const Suggestion& s)
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool stateChanged = s.severity != suggestion.severity || s.resolving != suggestion.resolving;
    const bool textDue = now - lastTextUpdateMs >= 1000
                         && (s.title != suggestion.title || s.detail != suggestion.detail || s.steps != suggestion.steps
                             || s.trackNames != suggestion.trackNames || describeAction (s.action) != describeAction (suggestion.action));
    if (! stateChanged && ! textDue)
        return;
    if (textDue)
        lastTextUpdateMs = now;
    suggestion = s;
    repaint();
}

DiagnosticCard::Layout DiagnosticCard::computeLayout (float width) const
{
    Layout l;
    const float w = width - 2 * kPad - 4.0f;
    float y = 14.0f;
    const float x = kPad + 4.0f;

    l.header = { x, y, w, 24.0f };                             y += 34.0f;
    const float th = textHeight (suggestion.title, titleFont(), w);
    l.title = { x, y, w, th };                                 y += th + 4.0f;
    if (! suggestion.trackNames.empty())
    {
        l.tracks = { x, y, w, 20.0f };                         y += 28.0f;
    }
    else
    {
        y += 6.0f;
    }
    const float dh = textHeight (suggestion.detail, bodyFont(), w);
    l.detail = { x, y, w, dh };                                y += dh + 10.0f;

    if (suggestion.action.type != ActionType::None)
    {
        l.action = { x, y, w, 30.0f };
        y += 42.0f;
    }

    for (const auto& step : suggestion.steps)
    {
        const float sh = juce::jmax (24.0f, textHeight (step, bodyFont(), w - kStepIndent));
        l.steps.push_back ({ x, y, w, sh });
        y += sh + 10.0f;
    }
    l.height = y + 8.0f;
    return l;
}

int DiagnosticCard::getHeightForWidth (int width) const
{
    return (int) std::ceil (computeLayout ((float) width).height);
}

void DiagnosticCard::resized()
{
    dismissButton.setBounds (getWidth() - (int) kPad - 80, 12, 80, 28);
}

void DiagnosticCard::paint (juce::Graphics& g)
{
    const auto l = computeLayout ((float) getWidth());
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool resolving = suggestion.resolving;
    const auto sev = resolving ? colours::meterGreen : suggestion.tip ? colours::amber : severityColour (suggestion.severity);

    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    // severity accent on the left edge
    g.setColour (sev);
    g.fillRoundedRectangle (bounds.withWidth (6.0f).reduced (0.0f, 10.0f), 3.0f);

    // header: severity badge, category chip, confidence
    auto header = l.header;
    {
        const auto label = resolving ? juce::String ("LOOKS FIXED") : suggestion.tip ? juce::String ("TIP") : severityLabel (suggestion.severity);
        const auto f = font (12.5f, true);
        const float bw = juce::GlyphArrangement::getStringWidth (f, label) + 20.0f;
        auto badge = header.removeFromLeft (bw);
        g.setColour (sev);
        g.fillRoundedRectangle (badge, 12.0f);
        g.setColour (colours::panelRaised);
        g.setFont (f);
        g.drawText (label, badge, juce::Justification::centred);
    }
    header.removeFromLeft (6.0f);
    if (! suggestion.tip)   // a tip's step heading already says what it is about
    {
        const auto label = categoryLabel (suggestion.category);
        const auto f = font (12.5f, true);
        const float bw = juce::GlyphArrangement::getStringWidth (f, label) + 20.0f;
        auto chip = header.removeFromLeft (bw);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (chip, 12.0f);
        g.setColour (colours::textDim);
        g.setFont (f);
        g.drawText (label, chip, juce::Justification::centred);
    }
    header.removeFromLeft (10.0f);
    g.setColour (colours::textFaint);
    g.setFont (smallFont());
    g.drawText (resolving        ? juce::String ("checking it stays fixed...")
                : suggestion.tip ? juce::String ("general advice: the plugin can't measure this one")
                                 : "confidence " + juce::String (juce::roundToInt (suggestion.confidence * 100.0f)) + "%",
                header.withTrimmedRight (90.0f), juce::Justification::centredLeft);

    drawWrapped (g, suggestion.title, titleFont(), colours::text, l.title);

    g.setColour (colours::accent);
    g.setFont (font (14.5f, true));
    g.drawText (trackLine (suggestion), l.tracks, juce::Justification::centredLeft);

    drawWrapped (g, suggestion.detail, bodyFont(), colours::textDim, l.detail);

    if (suggestion.action.type != ActionType::None)
    {
        const juce::String text = juce::String (describeAction (suggestion.action));
        const auto f = monoFont (14.5f);
        const float aw = juce::jmin (l.action.getWidth(), juce::GlyphArrangement::getStringWidth (f, text) + 40.0f);
        auto chip = l.action.withWidth (aw);
        // shown on a small dark display, like the read-out on the hardware
        g.setColour (colours::screen);
        g.fillRoundedRectangle (chip, 6.0f);
        g.setColour (colours::amber);
        g.setFont (font (14.0f, true));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb8"), chip.withWidth (26.0f), juce::Justification::centred);   // ▸
        g.setFont (f);
        g.setColour (colours::screenText);
        g.drawText (text, chip.withTrimmedLeft (26.0f), juce::Justification::centredLeft);
    }

    for (size_t i = 0; i < l.steps.size(); ++i)
    {
        auto r = l.steps[i];
        auto dot = juce::Rectangle<float> (r.getX(), r.getY(), 23.0f, 23.0f);
        g.setColour (colours::amber);
        g.fillEllipse (dot);
        g.setColour (colours::amberText);
        g.setFont (font (12.5f, true));
        g.drawText (juce::String ((int) i + 1), dot, juce::Justification::centred);
        drawWrapped (g, suggestion.steps[i], bodyFont(), colours::text, r.withTrimmedLeft (kStepIndent).withTrimmedTop (1.0f));
    }

    if (resolving)
    {
        g.setColour (colours::panelRaised.withAlpha (0.5f));   // fade the card while the fix is confirmed
        g.fillRoundedRectangle (bounds.withTrimmedTop (l.title.getY() - 4.0f), 10.0f);
    }
}

//==============================================================================
namespace
{
constexpr float kStepCircle = 34.0f;
constexpr float kStepTextX = kStepCircle + 14.0f;
const juce::Font stepTitleFont()   { return font (20.0f, true); }
const juce::Font stepSummaryFont() { return font (14.5f); }
}

int StepHeader::getHeightForWidth (int width) const
{
    return (int) std::ceil (6.0f + 26.0f + textHeight (stepSummary (step), stepSummaryFont(), (float) width - kStepTextX) + 6.0f);
}

void StepHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Numbered circle on a vertical line, like a step-by-step guide.
    auto circle = juce::Rectangle<float> (0.0f, 4.0f, kStepCircle, kStepCircle);
    g.setColour (colours::accent);
    g.fillEllipse (circle);
    g.setColour (colours::panelRaised);
    g.setFont (font (18.0f, true));
    g.drawText (juce::String ((int) step), circle, juce::Justification::centred);

    auto text = r.withTrimmedLeft (kStepTextX).withTrimmedTop (6.0f);
    auto titleRow = text.removeFromTop (26.0f);
    g.setColour (colours::text);
    g.setFont (stepTitleFont());
    const juce::String title (stepTitle (step));
    g.drawText (title, titleRow, juce::Justification::centredLeft);

    // Status beside the title: what was found for this step.
    const float tw = juce::GlyphArrangement::getStringWidth (stepTitleFont(), title);
    auto status = titleRow.withTrimmedLeft (tw + 12.0f);
    g.setFont (font (13.5f, true));
    if (problemCount > 0)
    {
        g.setColour (colours::accent);
        g.drawText (juce::String (problemCount) + (problemCount == 1 ? " thing to fix" : " things to fix"), status, juce::Justification::centredLeft);
    }
    else if (step != MixStep::Depth && step != MixStep::FinalTip)
    {
        g.setColour (juce::Colour (0xff2e9a57));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x9c\x93 nothing to fix here"), status, juce::Justification::centredLeft);   // ✓
    }

    drawWrapped (g, stepSummary (step), stepSummaryFont(), colours::textDim, text);
}

//==============================================================================
DiagnosticPanel::DiagnosticPanel()
{
    // "All" plus steps 1 - 5 (the final tip only shows under All).
    const char* labels[] = { "All", "1 Gain", "2 EQ", "3 Dyn", "4 Stereo", "5 Depth" };
    constexpr int numFilters = 6;
    for (int i = 0; i < numFilters; ++i)
    {
        auto* b = filterButtons.add (new juce::TextButton (labels[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setColour (juce::TextButton::buttonColourId, colours::panelRaised);
        b->setColour (juce::TextButton::buttonOnColourId, colours::amber);
        b->setColour (juce::TextButton::textColourOffId, colours::textDim);
        b->setColour (juce::TextButton::textColourOnId, colours::amberText);
        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < numFilters - 1 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, i]
        {
            stepFilter = i == 0 ? std::nullopt : std::optional<MixStep> ((MixStep) i);
            rebuild();
        };
        addAndMakeVisible (b);
    }
    filterButtons[0]->setToggleState (true, juce::dontSendNotification);

    expandButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    expandButton.setColour (juce::TextButton::textColourOffId, colours::panelRaised);
    expandButton.setTooltip ("Show the advice across the whole window");
    expandButton.onClick = [this] { setExpanded (! expanded); if (onExpandChanged) onExpandChanged (expanded); };
    addAndMakeVisible (expandButton);

    for (int i = 1; i <= kNumMixSteps; ++i)
        content.addChildComponent (headers.add (new StepHeader ((MixStep) i)));

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    emptyLabel.setText ("No issues detected yet. Press play: advice appears after a moment and stays until the problem is fixed.", juce::dontSendNotification);
    emptyLabel.setColour (juce::Label::textColourId, colours::textDim);
    emptyLabel.setFont (font (15.0f));
    emptyLabel.setJustificationType (juce::Justification::centred);
    content.addChildComponent (emptyLabel);
}

bool DiagnosticPanel::passesFilter (const Suggestion& s) const
{
    if (stepFilter.has_value() && s.step != *stepFilter)
        return false;
    if (trackFilter >= 0 && std::find (s.trackIds.begin(), s.trackIds.end(), (uint32_t) trackFilter) == s.trackIds.end())
        return false;
    return true;
}

void DiagnosticPanel::setSuggestions (const std::vector<Suggestion>& list)
{
    all = list;
    rebuild();
}

void DiagnosticPanel::setTrackFilter (int64_t trackId)
{
    trackFilter = trackId;
    rebuild();
}

void DiagnosticPanel::rebuild()
{
    // Cards are kept by key and updated in place: a card that stays on screen
    // is never destroyed and re-created, so nothing blinks or loses its place.
    std::map<std::string, std::unique_ptr<DiagnosticCard>> existing;
    while (! cards.isEmpty())
    {
        std::unique_ptr<DiagnosticCard> c (cards.removeAndReturn (0));
        existing[c->getSuggestion().key] = std::move (c);
    }

    for (const auto& s : all)
    {
        if (! passesFilter (s))
            continue;
        auto it = existing.find (s.key);
        if (it != existing.end())
        {
            it->second->update (s);
            cards.add (it->second.release());
            existing.erase (it);
            continue;
        }
        auto* card = cards.add (new DiagnosticCard (s));
        card->onDismiss = [this] (const std::string& key) { if (onDismiss) onDismiss (key); };
        content.addAndMakeVisible (card);
    }
    emptyLabel.setVisible (cards.isEmpty());
    layoutCards();
}

void DiagnosticPanel::layoutCards()
{
    const int w = juce::jmax (200, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    const bool filtered = stepFilter.has_value() || trackFilter >= 0;
    int y = 0;
    for (auto* h : headers)
    {
        int problems = 0, tips = 0;
        for (auto* c : cards)
            if (c->getSuggestion().step == h->getStep())
                (c->getSuggestion().tip ? tips : problems)++;

        // Every step is listed so the user can work top to bottom; with a
        // filter on, only the steps that have cards are.
        const bool show = ! cards.isEmpty() && (! filtered || problems + tips > 0);
        h->setVisible (show);
        if (! show)
            continue;
        h->setCounts (problems, tips);
        const int hh = h->getHeightForWidth (w);
        h->setBounds (0, y, w, hh);
        y += hh + 8;

        for (auto* c : cards)
        {
            if (c->getSuggestion().step != h->getStep())
                continue;
            const int ch = c->getHeightForWidth (w);
            c->setBounds (0, y, w, ch);
            y += ch + 12;
        }
        y += 10;
    }
    emptyLabel.setBounds (0, 0, w, 80);
    content.setSize (w, juce::jmax (y, 80));
}

void DiagnosticPanel::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (kHeaderHeight);
    g.setColour (colours::text);
    g.setFont (font (15.0f, true));
    g.drawText ("MIX ADVICE", top.withTrimmedLeft (2), juce::Justification::centredLeft);

    // severity counts
    int counts[3] {};
    for (const auto& s : all)
        if (! s.tip)
            counts[(int) s.severity]++;
    auto x = 116.0f;
    for (int sev = 2; sev >= 0; --sev)
    {
        auto r = juce::Rectangle<float> (x, 7.0f, 38.0f, 20.0f);
        g.setColour (severityColour ((Severity) sev).withAlpha (counts[sev] > 0 ? 1.0f : 0.25f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colours::panelRaised.withAlpha (counts[sev] > 0 ? 1.0f : 0.7f));
        g.setFont (font (13.0f, true));
        g.drawText (juce::String (counts[sev]), r, juce::Justification::centred);
        x += 44.0f;
    }

    if (trackFilter >= 0)
    {
        g.setColour (colours::accent);
        g.setFont (font (13.0f));
        g.drawText ("filtered to selected track", juce::Rectangle<float> (x + 6.0f, 7.0f, 200.0f, 20.0f), juce::Justification::centredLeft);
    }
}

void DiagnosticPanel::setExpanded (bool shouldExpand)
{
    expanded = shouldExpand;
    expandButton.setButtonText (expanded ? "Shrink" : "Enlarge");
}

void DiagnosticPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kHeaderHeight);
    expandButton.setBounds (top.removeFromRight (96).reduced (0, 3));
    top.removeFromRight (8);
    // Narrow panel: filters move to their own row instead of covering the counts.
    if (getWidth() < 820)
        top = r.removeFromTop (kHeaderHeight);
    auto buttons = top.removeFromRight (juce::jmin (440, top.getWidth())).reduced (0, 3);
    const int bw = buttons.getWidth() / filterButtons.size();
    for (auto* b : filterButtons)
        b->setBounds (buttons.removeFromLeft (bw));
    r.removeFromTop (8);
    viewport.setBounds (r);
    layoutCards();
}

} // namespace aimix::ui
