#pragma once

#include "ChainPanel.h"
#include "ChannelRack.h"
#include "DiagnosticCards.h"

namespace aimix::ui
{
// Mix-level stat tiles across the top of the Master Engine view.
class SummaryBar final : public juce::Component
{
public:
    void setReport (std::shared_ptr<const MixReport> r) { report = std::move (r); repaint(); }
    void setTargetLufs (float t) { targetLufs = t; }
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const MixReport> report;
    float targetLufs = -14.0f;
};

// Everything the Master Engine instance shows. Independent of the processor:
// it only consumes MixReports, so it can be rendered from tests and tools.
class MasterView final : public juce::Component
{
public:
    MasterView();

    void setReport (std::shared_ptr<const MixReport> report);
    std::function<void (const std::string&)> onDismiss;

    // Advice cards (with the channel rack), or the suggested mix bus chain.
    void showChain (bool shouldShow);
    bool isShowingChain() const noexcept { return chainButton.getToggleState(); }
    ChainPanel& getChainPanel() noexcept { return chain; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SummaryBar summary;
    ChannelRack rack;
    DiagnosticPanel diagnostics;
    ChainPanel chain;
    juce::TextButton adviceButton { "Advice" }, chainButton { "Mix bus chain" };
    std::shared_ptr<const MixReport> current;
};

} // namespace aimix::ui
