#pragma once

#include "PluginProcessor.h"
#include "ui/ListenerView.h"
#include "ui/MasterView.h"

class AIMixEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit AIMixEditor (AIMixProcessor&);
    ~AIMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For previews and headless checks: show this report instead of the
    // engine's live one.
    void setReportOverride (std::shared_ptr<const aimix::MixReport> report) { reportOverride = std::move (report); }
    void refresh() { timerCallback(); }

private:
    void timerCallback() override;
    void updateModeVisibility();

    AIMixProcessor& processor;

    juce::ComboBox modeBox, roleBox;
    juce::TextEditor nameEditor;
    juce::Label modeLabel, roleLabel, nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, roleAttachment;

    aimix::ui::ListenerView listenerView;
    aimix::ui::MasterView masterView;
    std::shared_ptr<const aimix::MixReport> reportOverride;
    AIMixProcessor::Mode shownMode { AIMixProcessor::Mode::Listener };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMixEditor)
};
