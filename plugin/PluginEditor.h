#pragma once

#include "PluginProcessor.h"

#include <optional>
#include "library/PluginLibraryService.h"
#include "ui/ListenerView.h"
#include "ui/MasterView.h"

class AIMixEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ChangeListener
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

    // For previews and headless checks: use this plugin list instead of the
    // one scanned from this computer.
    void setLibraryOverride (std::vector<aimix::PluginInfo> plugins);
    aimix::ui::MasterView& getMasterView() noexcept { return masterView; }
    aimix::ui::ListenerView& getListenerView() noexcept { return listenerView; }

private:
    static constexpr int kHeaderHeight = 62;

    void timerCallback() override;
    void updateModeVisibility();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void pushLibrary();
    void updateChainContext();

    AIMixProcessor& processor;
    aimix::ui::RetroLookAndFeel lookAndFeel;   // declared first so it outlives the child widgets

    juce::ComboBox modeBox, roleBox;
    juce::TextEditor nameEditor;
    juce::Label modeLabel, roleLabel, nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, roleAttachment;

    aimix::ui::ListenerView listenerView;
    aimix::ui::MasterView masterView;
    std::shared_ptr<const aimix::MixReport> reportOverride;
    juce::SharedResourcePointer<aimix::library::PluginLibraryService> library;
    std::optional<std::vector<aimix::PluginInfo>> libraryOverride;
    AIMixProcessor::Mode shownMode { AIMixProcessor::Mode::Listener };
    int chainTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMixEditor)
};
