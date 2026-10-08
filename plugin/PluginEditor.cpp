#include "PluginEditor.h"

using namespace aimix::ui;

AIMixEditor::AIMixEditor (AIMixProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto styleCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, colours::panelRaised);
        box.setColour (juce::ComboBox::outlineColourId, colours::outline);
        box.setColour (juce::ComboBox::textColourId, colours::text);
        box.setColour (juce::ComboBox::arrowColourId, colours::accent);
    };
    auto styleLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (11.5f, true));
        l.setColour (juce::Label::textColourId, colours::textDim);
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

    nameEditor.setTextToShowWhenEmpty ("name from host", colours::textFaint);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (8, 0);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, colours::panelRaised);
    nameEditor.setColour (juce::TextEditor::outlineColourId, colours::outline);
    nameEditor.setColour (juce::TextEditor::textColourId, colours::text);
    nameEditor.setText (processor.getTrackNameOverride(), false);   // after the colours: text takes the colour set when added
    nameEditor.onReturnKey = nameEditor.onFocusLost = [this] { processor.setTrackNameOverride (nameEditor.getText()); };

    for (juce::Component* c : { (juce::Component*) &modeBox, (juce::Component*) &roleBox, (juce::Component*) &nameEditor,
                                (juce::Component*) &modeLabel, (juce::Component*) &roleLabel, (juce::Component*) &nameLabel })
        addAndMakeVisible (c);

    addChildComponent (listenerView);
    addChildComponent (masterView);
    masterView.onDismiss = [this] (const std::string& key) { processor.dismissSuggestion (key); };
    listenerView.getChainPanel().onRescan = masterView.getChainPanel().onRescan = [this] { library->rescan(); pushLibrary(); };
    library->changes.addChangeListener (this);
    const auto daw = ChainPanel::dawFor (juce::PluginHostType().getHostDescription());
    for (auto* panel : { &listenerView.getChainPanel(), &masterView.getChainPanel() })
    {
        panel->setDaw (daw);
        panel->setAdded (processor.getChainAdded());
        panel->onAddedChanged = [this] (const juce::StringArray& added) { processor.setChainAdded (added); };
    }
    pushLibrary();

    styleButton.setColour (juce::TextButton::buttonColourId, colours::amber);
    styleButton.setColour (juce::TextButton::textColourOffId, colours::amberText);
    styleButton.setTooltip ("The genre and style the mix is judged against");
    styleButton.onClick = [this] { showStylePicker (true); };
    addAndMakeVisible (styleButton);

    addChildComponent (stylePicker);
    stylePicker.onChosen = [this] (aimix::MixStyle chosen)
    {
        processor.chooseStyle (chosen);
        showStylePicker (false);
        updateStyle();
    };
    stylePicker.onSkip = [this] { pickerSkipped = true; showStylePicker (false); };

    setResizable (true, true);
    setResizeLimits (960, 620, 2600, 1700);
    setSize (1400, 900);

    updateModeVisibility();
    updateStyle();
    startTimerHz (30);
}

void AIMixEditor::showStylePicker (bool show)
{
    if (show)
    {
        const auto current = processor.getEffectiveStyle();
        stylePicker.setStyle (current.isSet() ? current : aimix::MixStyle {});
        stylePicker.toFront (false);
    }
    stylePicker.setVisible (show);
}

void AIMixEditor::updateStyleButtonText()
{
    const bool roomy = styleButton.getWidth() >= 200;
    styleButton.setButtonText (shownStyle.isSet() ? juce::String (aimix::styleName (shownStyle)) + (roomy ? "  |  Change" : "")
                                                  : juce::String ("Choose genre"));
}

// Follows the chosen genre/style: header button, loudness target, chain picks.
// Before one is chosen the picker covers the window.
void AIMixEditor::updateStyle()
{
    const auto style = processor.getEffectiveStyle();
    if (style != shownStyle || styleButton.getButtonText().isEmpty())
    {
        shownStyle = style;
        updateStyleButtonText();
        masterView.setTargetLufs (aimix::profileFor (style).targetLufs);
        updateChainContext();
    }
    if (! style.isSet() && ! pickerSkipped && ! stylePicker.isVisible())
        showStylePicker (true);
}

AIMixEditor::~AIMixEditor()
{
    stopTimer();
    library->changes.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AIMixEditor::updateModeVisibility()
{
    shownMode = processor.getMode();
    const bool master = shownMode == AIMixProcessor::Mode::Master;
    masterView.setVisible (master);
    listenerView.setVisible (! master);
    // The mix bus needs no role or track name: hide them rather than asking.
    for (juce::Component* c : { (juce::Component*) &roleBox, (juce::Component*) &roleLabel,
                                (juce::Component*) &nameEditor, (juce::Component*) &nameLabel })
        c->setVisible (! master);
    updateChainContext();
    repaint();
}

void AIMixEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pushLibrary();
}

void AIMixEditor::setLibraryOverride (std::vector<aimix::PluginInfo> plugins)
{
    libraryOverride = std::move (plugins);
    pushLibrary();
}

void AIMixEditor::pushLibrary()
{
    std::vector<aimix::PluginInfo> plugins;
    juce::String status;
    bool scanning = false;
    if (libraryOverride.has_value())
    {
        plugins = *libraryOverride;
        int analog = 0;
        for (const auto& p : plugins)
            analog += p.character == aimix::PluginCharacter::Analog ? 1 : 0;
        status = "Found " + juce::String ((int) plugins.size()) + " plugins, " + juce::String (analog) + " analog models";
    }
    else
    {
        plugins = library->getPlugins();
        status = library->getStatusText();
        scanning = library->isScanning();
    }
    listenerView.getChainPanel().setLibrary (plugins, status, scanning);
    masterView.getChainPanel().setLibrary (std::move (plugins), status, scanning);
    updateChainContext();
}

void AIMixEditor::updateChainContext()
{
    aimix::ChainContext ctx;
    ctx.preferredFormat = processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit ? "AU" : "VST3";

    if (shownMode == AIMixProcessor::Mode::Master)
    {
        auto report = reportOverride != nullptr ? reportOverride : processor.getLatestReport();
        if (report != nullptr && report->hasMaster)
        {
            const auto measured = aimix::chainContextFor (report->master.view, true);
            ctx = measured;
            ctx.preferredFormat = processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit ? "AU" : "VST3";
        }
        ctx.master = true;
        ctx.role = aimix::TrackRole::MasterBus;
        ctx.style = processor.getEffectiveStyle();
        masterView.getChainPanel().setContext (ctx);
        return;
    }

    const auto& m = processor.getMeters();
    ctx.role = processor.getEffectiveRole();
    const float peak = juce::jmax (m.peakDb[0].load(), m.peakDb[1].load());
    const float rms = juce::jmax (m.rmsDb[0].load(), m.rmsDb[1].load());
    ctx.measured = peak > -90.0f;
    ctx.peakDb = peak;
    // Round so the suggestion text doesn't flicker with every meter update.
    ctx.crestDb = std::round (peak - rms);
    ctx.sideToMidDb = m.sideToMidDb.load();
    ctx.style = processor.getEffectiveStyle();
    listenerView.getChainPanel().setContext (ctx);
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
        s.roleText = processor.getRoleDisplay();
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

    // The chain only changes with the role or big changes in the sound.
    if (++chainTick % 15 == 0)
    {
        updateStyle();
        updateChainContext();
    }
}

void AIMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (kHeaderHeight).toFloat();
    g.setColour (colours::panel);
    g.fillRect (header);
    g.setColour (colours::outline);
    g.fillRect (header.removeFromBottom (1.0f));

    // Logo with a black-to-red underline, like the badge on the hardware.
    const auto logoFont = font (22.0f, true);
    const juce::String logo ("K MASTER");
    const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, logo);
    g.setColour (colours::text);
    g.setFont (logoFont);
    g.drawText (logo, juce::Rectangle<float> (20.0f, 8.0f, logoW + 4.0f, 34.0f), juce::Justification::centredLeft);
    auto underline = juce::Rectangle<float> (20.0f, 42.0f, logoW, 4.0f);
    g.setGradientFill (juce::ColourGradient (colours::text, underline.getX(), 0.0f, colours::accent, underline.getRight(), 0.0f, false));
    g.fillRect (underline);

    // Small dark display with what this instance is doing.
    const bool master = shownMode == AIMixProcessor::Mode::Master;
    auto display = juce::Rectangle<float> (logoW + 44.0f, 14.0f, 230.0f, 34.0f);
    if (display.getRight() < (float) styleButton.getX() - 8.0f)
    {
        g.setColour (colours::screen);
        g.fillRoundedRectangle (display, 6.0f);
        g.setColour (colours::screenText);
        g.setFont (monoFont (14.0f));
        g.drawFittedText (master ? juce::String ("MASTER ENGINE") : processor.getEffectiveTrackName().toUpperCase(),
                          display.reduced (12.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void AIMixEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderHeight).reduced (20, 15);

    nameEditor.setBounds (header.removeFromRight (170));
    nameLabel.setBounds (header.removeFromRight (56));
    header.removeFromRight (8);
    roleBox.setBounds (header.removeFromRight (120));
    roleLabel.setBounds (header.removeFromRight (48));
    header.removeFromRight (8);
    modeBox.setBounds (header.removeFromRight (150));
    modeLabel.setBounds (header.removeFromRight (52));
    header.removeFromRight (14);
    styleButton.setBounds (header.removeFromRight (juce::jlimit (130, 230, header.getWidth() - 130)));
    updateStyleButtonText();

    auto body = r.reduced (20, 16);
    listenerView.setBounds (body);
    masterView.setBounds (body);
    stylePicker.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight));
}
