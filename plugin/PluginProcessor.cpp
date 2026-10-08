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
    juce::StringArray genres, eras;
    for (int g = 0; g < aimix::kNumGenres; ++g)
        genres.add (aimix::genreName ((aimix::Genre) g));
    for (int e = 0; e < aimix::kNumEras; ++e)
        eras.add (aimix::eraName ((aimix::Era) e));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "genre", 1 }, "Genre", genres, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "era", 1 }, "Style", eras, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}
}

juce::StringArray AIMixProcessor::roleNames()
{
    juce::StringArray names;
    // Index 0 (TrackRole::Unknown on the bus) means "detect it for me".
    names.add ("Auto");
    for (int r = 1; r < (int) aimix::TrackRole::MasterBus; ++r)
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
    genreParam = parameters.getRawParameterValue ("genre");
    eraParam = parameters.getRawParameterValue ("era");

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
    state.setProperty ("chainAdded", chainAdded.joinIntoString ("\n"), nullptr);
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
            chainAdded = juce::StringArray::fromLines (state.getProperty ("chainAdded").toString());
            chainAdded.removeEmptyStrings();
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

juce::String AIMixProcessor::getRoleDisplay() const
{
    const auto chosen = (aimix::TrackRole) juce::roundToInt (roleParam->load());
    if (chosen != aimix::TrackRole::Unknown)
        return aimix::toString (chosen);

    const int slot = getBusSlot();
    const auto detected = slot >= 0 && bus != nullptr ? bus->slot (slot).detectedRole.load (std::memory_order_relaxed) : 0u;
    if ((detected & aimix::kDetectedRoleValid) != 0)
        return aimix::ui::roleText ((aimix::TrackRole) (detected & 0xffu)) + " (detected)";
    return isMasterEngineOnline() ? "Auto: listening..." : "Auto";
}

aimix::TrackRole AIMixProcessor::getEffectiveRole() const
{
    if (getMode() == Mode::Master)
        return aimix::TrackRole::MasterBus;
    const auto chosen = (aimix::TrackRole) juce::roundToInt (roleParam->load());
    if (chosen != aimix::TrackRole::Unknown)
        return chosen;
    const int slot = getBusSlot();
    const auto detected = slot >= 0 && bus != nullptr ? bus->slot (slot).detectedRole.load (std::memory_order_relaxed) : 0u;
    return (detected & aimix::kDetectedRoleValid) != 0 ? (aimix::TrackRole) (detected & 0xffu) : aimix::TrackRole::Unknown;
}

aimix::MixStyle AIMixProcessor::getOwnStyle() const noexcept
{
    aimix::MixStyle s;
    s.genre = (aimix::Genre) juce::jlimit (0, aimix::kNumGenres - 1, juce::roundToInt (genreParam->load()));
    s.era = (aimix::Era) juce::jlimit (0, aimix::kNumEras - 1, juce::roundToInt (eraParam->load()));
    return s;
}

aimix::MixStyle AIMixProcessor::getEffectiveStyle() const noexcept
{
    if (getMode() == Mode::Listener && isMasterEngineOnline())
    {
        const auto shared = aimix::MixStyle::unpack (bus->layout().header.mixStyle.load (std::memory_order_relaxed));
        if (shared.isSet())
            return shared;
    }
    return getOwnStyle();
}

void AIMixProcessor::chooseStyle (aimix::MixStyle style)
{
    auto set = [this] (const char* id, int index)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
    };
    set ("genre", (int) style.genre);
    set ("era", (int) style.era);

    // Ask the Master Engine (if any) to use it for the whole session.
    if (getMode() == Mode::Listener && bus != nullptr)
    {
        auto& req = bus->layout().header.requestedStyle;
        const auto old = req.load();
        req.store ((((old >> 16) + 1) << 16) | (style.pack() & 0xffffu));
    }
    if (engine != nullptr)
        engine->setStyle (style);
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
        // A style picked on a track before this Master existed is adopted;
        // an older one is not when this Master already has its own.
        lastStyleRequest = getOwnStyle().isSet() ? bus->layout().header.requestedStyle.load() : 0u;
    }
    else if (! wantEngine && engine != nullptr)
    {
        engine.reset();
    }

    if (engine != nullptr)
    {
        const auto request = bus->layout().header.requestedStyle.load();
        if (request != lastStyleRequest)
        {
            lastStyleRequest = request;
            const auto picked = aimix::MixStyle::unpack (request & 0xffffu);
            if (picked.isSet() && picked != getOwnStyle())
                chooseStyle (picked);
        }
        engine->setStyle (getOwnStyle());
    }
    pushIdentityToAnalyzer();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIMixProcessor();
}
