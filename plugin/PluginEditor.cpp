#include "PluginEditor.h"

using namespace aimix::ui;

AIMixEditor::AIMixEditor (AIMixProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    auto styleCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, colours::panel);
        box.setColour (juce::ComboBox::outlineColourId, colours::outline);
        box.setColour (juce::ComboBox::textColourId, colours::text);
        box.setColour (juce::ComboBox::arrowColourId, colours::textDim);
    };
    auto styleLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (11.0f, true));
        l.setColour (juce::Label::textColourId, colours::textFaint);
        l.setJustificationType (juce::Justification::centredRight);
    };

    modeBox.addItemList ({ "Listener", "Master Engine" }, 1);
    roleBox.addItemList (AIMixProcessor::roleNames(), 1);
    styleCombo (modeBox);
    styleCombo (roleBox);
    styleLabel (modeLabel, "MODE");
    styleLabel (roleLabel, "ROLE");
    styleLabel (nameLabel, "TRACK");

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, "mode", modeBox);
    roleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, "role", roleBox);
    modeBox.onChange = [this] { processor.syncModeNow(); updateModeVisibility(); };

    nameEditor.setText (processor.getTrackNameOverride(), false);
    nameEditor.setTextToShowWhenEmpty ("name from host", colours::textFaint);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (8, 0);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, colours::panel);
    nameEditor.setColour (juce::TextEditor::outlineColourId, colours::outline);
    nameEditor.setColour (juce::TextEditor::textColourId, colours::text);
    nameEditor.onReturnKey = nameEditor.onFocusLost = [this] { processor.setTrackNameOverride (nameEditor.getText()); };

    for (juce::Component* c : { (juce::Component*) &modeBox, (juce::Component*) &roleBox, (juce::Component*) &nameEditor,
                                (juce::Component*) &modeLabel, (juce::Component*) &roleLabel, (juce::Component*) &nameLabel })
        addAndMakeVisible (c);

    addChildComponent (listenerView);
    addChildComponent (masterView);
    masterView.onDismiss = [this] (const std::string& key) { processor.dismissSuggestion (key); };

    setResizable (true, true);
    setResizeLimits (880, 560, 2400, 1600);
    setSize (1240, 760);

    updateModeVisibility();
    startTimerHz (30);
}

AIMixEditor::~AIMixEditor() { stopTimer(); }

void AIMixEditor::updateModeVisibility()
{
    shownMode = processor.getMode();
    const bool master = shownMode == AIMixProcessor::Mode::Master;
    masterView.setVisible (master);
    listenerView.setVisible (! master);
    roleBox.setEnabled (! master);
    roleLabel.setAlpha (master ? 0.4f : 1.0f);
    repaint();
}

void AIMixEditor::timerCallback()
{
    if (processor.getMode() != shownMode)
        updateModeVisibility();

    if (shownMode == AIMixProcessor::Mode::Master)
    {
        masterView.setReport (reportOverride != nullptr ? reportOverride : processor.getLatestReport());
    }
    else
    {
        const auto& m = processor.getMeters();
        ListenerView::Status s;
        s.trackName = processor.getEffectiveTrackName();
        s.slot = processor.getBusSlot();
        s.sharedMemory = processor.isUsingSharedMemory();
        s.masterOnline = processor.isMasterEngineOnline();
        s.framesSent = processor.getFramesSent();
        s.framesDropped = processor.getFramesDropped();
        for (int c = 0; c < 2; ++c)
        {
            s.rmsDb[c] = m.rmsDb[c].load();
            s.peakDb[c] = m.peakDb[c].load();
        }
        s.momentary = m.momentaryLufs.load();
        s.shortTerm = m.shortTermLufs.load();
        s.integrated = m.integratedLufs.load();
        s.correlation = m.correlation.load();
        s.sideToMidDb = m.sideToMidDb.load();
        listenerView.setStatus (s);
    }
}

void AIMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (52).toFloat();
    g.setColour (colours::panel);
    g.fillRect (header);
    g.setColour (colours::outline);
    g.fillRect (header.removeFromBottom (1.0f));

    g.setColour (colours::text);
    g.setFont (font (17.0f, true));
    g.drawText ("AI Mix Assistant", juce::Rectangle<float> (18.0f, 0.0f, 200.0f, 52.0f), juce::Justification::centredLeft);
    g.setColour (colours::textFaint);
    g.setFont (font (12.0f));
    const bool master = shownMode == AIMixProcessor::Mode::Master;
    g.drawText (master ? "Master Engine" : processor.getEffectiveTrackName(),
                juce::Rectangle<float> (176.0f, 0.0f, 220.0f, 52.0f), juce::Justification::centredLeft);
}

void AIMixEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (52).reduced (16, 12);

    nameEditor.setBounds (header.removeFromRight (170));
    nameLabel.setBounds (header.removeFromRight (56));
    header.removeFromRight (8);
    roleBox.setBounds (header.removeFromRight (120));
    roleLabel.setBounds (header.removeFromRight (48));
    header.removeFromRight (8);
    modeBox.setBounds (header.removeFromRight (150));
    modeLabel.setBounds (header.removeFromRight (52));

    auto body = r.reduced (16, 14);
    listenerView.setBounds (body);
    masterView.setBounds (body);
}
