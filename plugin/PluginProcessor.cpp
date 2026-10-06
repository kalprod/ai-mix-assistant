#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Listener", "Master Engine" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "role", 1 }, "Track Role", AIMixProcessor::roleNames(), 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}
}

juce::StringArray AIMixProcessor::roleNames()
{
    juce::StringArray names;
    for (int r = 0; r < (int) aimix::TrackRole::MasterBus; ++r)
        names.add (aimix::toString ((aimix::TrackRole) r));
    return names;
}

AIMixProcessor::AIMixProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AIMixState", createLayout())
{
    modeParam = parameters.getRawParameterValue ("mode");
    roleParam = parameters.getRawParameterValue ("role");

    // Shared memory first so Listeners in sandboxed/out-of-process hosts can
    // still reach the Master; falls back to an in-process bus.
    bus = aimix::SharedBus::open();
    publisher = std::make_unique<aimix::BusPublisher> (bus);

    pushIdentityToAnalyzer();
    startTimerHz (4);
}

AIMixProcessor::~AIMixProcessor()
{
    stopTimer();
    engine.reset();       // joins the engine thread, releases master ownership
    publisher.reset();    // frees the bus slot
}

//==============================================================================
void AIMixProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    pushIdentityToAnalyzer();
}

void AIMixProcessor::releaseResources() {}

bool AIMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;   // pass-through only: never invent or drop channels
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AIMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    aimix::TimelineInfo timeline;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto samples = position->getTimeInSamples(); samples.hasValue() && position->getIsPlaying())
                timeline = { *samples, true };

    const int numChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (numChannels > 0)
        analyzer.process (buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples(), timeline, *publisher);

    // Non-destructive: the buffer is only ever read. Input and output layouts
    // are identical, so there are no extra output channels to clear either.
}

void AIMixProcessor::processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&)
{
    // Never called: supportsDoublePrecisionProcessing() is false.
}

//==============================================================================
juce::AudioProcessorEditor* AIMixProcessor::createEditor()
{
    return new AIMixEditor (*this);
}

void AIMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("trackName", trackNameOverride, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AIMixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            trackNameOverride = state.getProperty ("trackName").toString();
            parameters.replaceState (state);
            pushIdentityToAnalyzer();
        }
}

void AIMixProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // May arrive on any thread in some hosts; bounce to the message thread.
    juce::MessageManager::callAsync ([safe = juce::WeakReference<AIMixProcessor> (this), name = properties.name.value_or (juce::String())]
    {
        if (auto* self = safe.get())
        {
            self->hostTrackName = name;
            self->pushIdentityToAnalyzer();
        }
    });
}

//==============================================================================
AIMixProcessor::Mode AIMixProcessor::getMode() const noexcept
{
    return modeParam->load() >= 0.5f ? Mode::Master : Mode::Listener;
}

std::shared_ptr<const aimix::MixReport> AIMixProcessor::getLatestReport() const
{
    return engine != nullptr ? engine->getLatestReport() : nullptr;
}

bool AIMixProcessor::isUsingSharedMemory() const noexcept
{
    return bus != nullptr && bus->backend() == aimix::SharedBus::Backend::SharedMemory;
}

bool AIMixProcessor::isMasterEngineOnline() const noexcept
{
    return bus != nullptr && bus->isMasterAlive (aimix::monotonicMillis());
}

void AIMixProcessor::dismissSuggestion (const std::string& key)
{
    if (engine != nullptr)
        engine->dismiss (key);
}

juce::String AIMixProcessor::getEffectiveTrackName() const
{
    if (trackNameOverride.isNotEmpty()) return trackNameOverride;
    if (getMode() == Mode::Master)      return "Mix Bus";
    if (hostTrackName.isNotEmpty())     return hostTrackName;
    return "Track " + juce::String (getBusSlot() + 1);
}

void AIMixProcessor::setTrackNameOverride (const juce::String& name)
{
    trackNameOverride = name.trim();
    pushIdentityToAnalyzer();
}

void AIMixProcessor::pushIdentityToAnalyzer()
{
    const bool master = getMode() == Mode::Master;
    analyzer.setIsMasterBus (master);
    analyzer.setRole (master ? aimix::TrackRole::MasterBus : (aimix::TrackRole) juce::roundToInt (roleParam->load()));
    analyzer.setTrackName (getEffectiveTrackName().toRawUTF8());
}

void AIMixProcessor::timerCallback()
{
    const bool wantEngine = getMode() == Mode::Master;
    if (wantEngine && engine == nullptr && bus != nullptr)
    {
        engine = std::make_unique<aimix::MixEngine> (bus);
        engine->start();
    }
    else if (! wantEngine && engine != nullptr)
    {
        engine.reset();
    }
    pushIdentityToAnalyzer();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIMixProcessor();
}
