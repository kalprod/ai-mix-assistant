#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "aimix/MixEngine.h"
#include "aimix/SharedBus.h"
#include "aimix/TrackAnalyzer.h"

// One plugin binary, two roles chosen by the "mode" parameter:
//
//   Listener       insert on each track. Analyses the track (const access only,
//                  audio passes through bit-identical) and publishes one
//                  AnalysisPayload per 2048-sample hop to its bus slot.
//   Master Engine  insert on the mix bus. Publishes its own mix-bus analysis
//                  and runs the MixEngine thread that drains every slot, runs
//                  the rules and feeds the channel rack / diagnostic UI.
class AIMixProcessor final : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    enum class Mode { Listener = 0, Master = 1 };

    AIMixProcessor();
    ~AIMixProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }   // VST3 validator rejects unnamed programs
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Host track name (VST3 IInfoListener / AU kAudioUnitProperty_...): used as
    // the default track name so users rarely need to type one.
    void updateTrackProperties (const TrackProperties& properties) override;

    //==============================================================================
    // Message-thread API for the editor.
    Mode getMode() const noexcept;
    std::shared_ptr<const aimix::MixReport> getLatestReport() const;
    const aimix::ListenerMeters& getMeters() const noexcept { return analyzer.getMeters(); }
    uint64_t getFramesSent() const noexcept { return analyzer.getFramesEmitted(); }
    uint64_t getFramesDropped() const noexcept { return analyzer.getFramesDropped(); }
    int getBusSlot() const noexcept { return publisher != nullptr ? publisher->slot() : -1; }
    bool isUsingSharedMemory() const noexcept;
    bool isMasterEngineOnline() const noexcept;
    void dismissSuggestion (const std::string& key);

    juce::String getEffectiveTrackName() const;
    juce::String getRoleDisplay() const;   // chosen role, or what the Master Engine detected for "Auto"
    void setTrackNameOverride (const juce::String& name);
    juce::String getTrackNameOverride() const { return trackNameOverride; }

    // Make the mode/engine state follow the parameters immediately (normally
    // done by a 4 Hz timer). Used by the editor and by the headless checks.
    void syncModeNow() { timerCallback(); }

    juce::AudioProcessorValueTreeState parameters;

    static juce::StringArray roleNames();

private:
    void timerCallback() override;
    void pushIdentityToAnalyzer();

    std::shared_ptr<aimix::SharedBus> bus;
    std::unique_ptr<aimix::BusPublisher> publisher;
    aimix::TrackAnalyzer analyzer;
    std::unique_ptr<aimix::MixEngine> engine;   // message thread only

    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* roleParam = nullptr;

    juce::String trackNameOverride, hostTrackName;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AIMixProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMixProcessor)
};
