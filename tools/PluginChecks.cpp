// Host-compatibility checks run against the real JUCE AudioProcessor/Editor
// (the same code every plugin wrapper - VST3, AU, AAX, Standalone - calls into),
// plus PNG snapshots of both editor modes.
//
//   aimix_plugin_checks [output-dir]
//
// Format-specific wrapper validation (VST3 / AU / AAX) is done separately with
// pluginval, auval and the AAX Validator; see docs/HOST_COMPATIBILITY.md.

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"
#include "../plugin/library/PluginLibraryService.h"
#include "aimix/SyntheticSession.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>

static std::atomic<long long> allocations { 0 };
void* operator new (std::size_t n)
{
    allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "[PASS]" : "[FAIL]", what.toRawUTF8());
    if (! ok) ++failures;
}

struct FakePlayHead final : juce::AudioPlayHead
{
    int64_t position = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples (position);
        info.setIsPlaying (playing);
        info.setBpm (120.0);
        return info;
    }
};

void savePng (juce::Component& c, const juce::File& file)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    check (ok, "wrote " + file.getFullPathName());
}

void setParam (AIMixProcessor& p, const juce::String& id, float normalised)
{
    p.parameters.getParameter (id)->setValueNotifyingHost (normalised);
}

//==============================================================================
void checkLayouts()
{
    AIMixProcessor p;
    using CS = juce::AudioChannelSet;
    auto layout = [] (CS in, CS out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l; };
    check (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stereo in/out supported");
    check (p.checkBusesLayoutSupported (layout (CS::mono(), CS::mono())), "mono in/out supported");
    check (! p.checkBusesLayoutSupported (layout (CS::mono(), CS::stereo())), "mono->stereo rejected (pass-through only)");
    check (! p.checkBusesLayoutSupported (layout (CS::create5point1(), CS::create5point1())), "5.1 rejected");
    check (p.getLatencySamples() == 0, "reports zero latency");
    check (p.getTailLengthSeconds() == 0.0, "reports zero tail");
}

void checkNonDestructiveAndRealtime()
{
    AIMixProcessor p;
    FakePlayHead head;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (2, 2, 48000.0, 2048);
    p.prepareToPlay (48000.0, 2048);

    std::mt19937 rng (42);
    std::uniform_int_distribution<int> sizeDist (1, 2048);
    std::uniform_real_distribution<float> sample (-1.0f, 1.0f);
    juce::AudioBuffer<float> buffer (2, 2048), copy (2, 2048);
    juce::MidiBuffer midi;

    bool identical = true;
    long long allocs = 0;
    for (int block = 0; block < 3000; ++block)
    {
        const int n = sizeDist (rng);   // hosts may send any size up to the maximum, including odd ones
        buffer.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                buffer.setSample (c, i, sample (rng));
        copy.makeCopyOf (buffer, true);

        head.playing = (block / 500) % 2 == 0;   // toggle transport every 500 blocks
        if (block == 1700) head.position = 123457;   // seek

        const auto before = allocations.load();
        p.processBlock (buffer, midi);
        allocs += allocations.load() - before;
        head.position += n;

        for (int c = 0; c < 2 && identical; ++c)
            identical = std::memcmp (buffer.getReadPointer (c), copy.getReadPointer (c), sizeof (float) * (size_t) n) == 0;
    }
    check (identical, "processBlock leaves audio bit-identical (3000 random-size blocks, seeks, transport toggles)");
    check (allocs == 0, "processBlock performs no heap allocation (" + juce::String (allocs) + ")");

    // Silence / denormal input must not explode CPU or produce NaNs downstream.
    buffer.setSize (2, 512);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (c, i, 1.0e-39f);
    p.processBlock (buffer, midi);
    check (p.getMeters().momentaryLufs.load() == p.getMeters().momentaryLufs.load(), "denormal input yields finite meters");

    p.releaseResources();
    p.prepareToPlay (96000.0, 64);   // re-prepare at another rate / block size
    buffer.setSize (2, 64);
    buffer.clear();
    p.processBlock (buffer, midi);
    check (true, "re-prepare at 96 kHz / 64 samples");
}

void checkState()
{
    juce::MemoryBlock state;
    {
        AIMixProcessor p;
        setParam (p, "mode", 1.0f);
        setParam (p, "role", p.parameters.getParameter ("role")->convertTo0to1 (5.0f));   // Bass
        p.setTrackNameOverride ("Bass DI");
        p.getStateInformation (state);
    }
    AIMixProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    check (restored.getMode() == AIMixProcessor::Mode::Master, "state restores mode");
    check (juce::roundToInt (restored.parameters.getRawParameterValue ("role")->load()) == 5, "state restores role");
    check (restored.getTrackNameOverride() == "Bass DI", "state restores track name");

    restored.setStateInformation ("garbage", 7);
    check (true, "garbage state is ignored without crashing");
}

// A typical studio plugin folder, for snapshots that don't depend on what is
// installed on the machine running the checks.
std::vector<aimix::PluginInfo> demoLibrary()
{
    struct Raw { const char* name; const char* vendor; const char* format; const char* category; };
    const Raw raw[] = {
        { "Pro-Q 3", "FabFilter", "AU", "Fx|EQ" },
        { "Pro-L 2", "FabFilter", "AU", "Fx|Dynamics" },
        { "Pro-C 2", "FabFilter", "AU", "Fx|Dynamics" },
        { "Pro-DS", "FabFilter", "AU", "Fx|Dynamics" },
        { "UADx Pultec EQP-1A", "Universal Audio", "AU", "" },
        { "UADx 1176 Rev E", "Universal Audio", "AU", "" },
        { "UADx LA-2A Gray", "Universal Audio", "AU", "" },
        { "UADx SSL G Bus Compressor", "Universal Audio", "AU", "" },
        { "UADx API Vision Channel Strip", "Universal Audio", "AU", "" },
        { "Pre 1973", "Arturia", "AU", "" },
        { "Comp FET-76", "Arturia", "AU", "" },
        { "J37 Tape", "Waves", "AU", "" },
        { "Kramer Master Tape", "Waves", "AU", "" },
        { "Rev PLATE-140", "Arturia", "AU", "" },
        { "Decapitator", "Soundtoys", "AU", "" },
        { "AUNBandEQ", "Apple", "AU", "EQ" },
        { "AUPeakLimiter", "Apple", "AU", "Effect" },
        { "ChannelEQ", "Apple", "AU", "EQ" },
    };
    std::vector<aimix::PluginInfo> out;
    for (const auto& r : raw)
    {
        aimix::PluginInfo p;
        p.name = r.name;
        p.vendor = r.vendor;
        p.format = r.format;
        p.reportedCategory = r.category;
        p.id = std::string (r.format) + ":" + r.vendor + "/" + r.name;
        aimix::tagPlugin (p);
        out.push_back (std::move (p));
    }
    return out;
}

void checkPluginScanner()
{
    using namespace aimix::library;
    juce::TemporaryFile tmp;
    const auto root = tmp.getFile();
    root.createDirectory();

    // A VST3 bundle with moduleinfo.json (JSON5 trailing commas, one instrument to skip).
    const auto withInfo = root.getChildFile ("Warm Bus.vst3");
    withInfo.getChildFile ("Contents/Resources").createDirectory();
    withInfo.getChildFile ("Contents/Resources/moduleinfo.json").replaceWithText (R"({
        "Name": "Warm Bus", "Version": "1.2.0",
        "Factory Info": { "Vendor": "Analog Co", },
        "Classes": [
            { "CID": "ABC123", "Category": "Audio Module Class", "Name": "Warm Bus Comp", "Vendor": "Analog Co",
              "Version": "1.2.0", "Sub Categories": [ "Fx", "Dynamics", ], },
            { "CID": "DEF456", "Category": "Audio Module Class", "Name": "Warm Synth",
              "Sub Categories": [ "Instrument", "Synth" ] },
            { "CID": "GHI789", "Category": "Component Controller Class", "Name": "Warm Bus Comp" },
        ],
    })");

    // A macOS-style bundle with only an Info.plist.
    const auto withPlist = root.getChildFile ("Some EQ.vst3");
    withPlist.getChildFile ("Contents").createDirectory();
    withPlist.getChildFile ("Contents/Info.plist").replaceWithText (
        "<plist><dict><key>CFBundleIdentifier</key><string>com.pultecstyle.someeq</string>"
        "<key>CFBundleName</key><string>Pultec Style EQ</string>"
        "<key>CFBundleShortVersionString</key><string>2.0</string></dict></plist>");

    ScanOptions options;
    options.audioUnits = false;
    options.vst2 = false;
    options.extraVst3Folders.add (root);
    const auto found = scanInstalledPlugins (options);

    auto findByName = [&found] (const char* name) -> const aimix::PluginInfo*
    {
        for (const auto& p : found)
            if (p.name == name)
                return &p;
        return nullptr;
    };
    const auto* comp = findByName ("Warm Bus Comp");
    check (comp != nullptr && comp->vendor == "Analog Co" && comp->does (aimix::kFnCompressor)
               && comp->character == aimix::PluginCharacter::AnalogInspired,
           "VST3 scan reads moduleinfo.json and tags a 'warm' compressor as analog-style");
    check (findByName ("Warm Synth") == nullptr, "VST3 scan skips instruments");
    const auto* eq = findByName ("Pultec Style EQ");
    check (eq != nullptr && eq->family == "Pultec" && eq->version == "2.0", "VST3 scan falls back to Info.plist");

    // Save, load, and keep the user's own changes across a rescan.
    auto lib = demoLibrary();
    lib[0].favourite = true;
    lib[1].tagSource = aimix::TagSource::User;
    lib[1].character = aimix::PluginCharacter::AnalogInspired;
    int64_t when = 0;
    const auto json = juce::JSON::toString (PluginLibraryService::toJson (lib, 1234));
    auto loaded = PluginLibraryService::fromJson (juce::JSON::parse (json), &when);
    bool same = loaded.size() == lib.size() && when == 1234;
    for (size_t i = 0; same && i < lib.size(); ++i)
        same = loaded[i].id == lib[i].id && loaded[i].functions == lib[i].functions && loaded[i].family == lib[i].family
            && loaded[i].character == lib[i].character && loaded[i].favourite == lib[i].favourite;
    check (same, "plugin library saves and loads as JSON");

    auto fresh = demoLibrary();
    PluginLibraryService::keepUserChanges (fresh, loaded);
    check (fresh[0].favourite && fresh[1].character == aimix::PluginCharacter::AnalogInspired,
           "a rescan keeps favourites and tags you corrected");
    root.deleteRecursively();
}

std::shared_ptr<const aimix::MixReport> checkMultiInstance (const juce::File& outDir)
{
    aimix::SharedBus::unlinkSharedMemory (aimix::kDefaultBusName);

    AIMixProcessor kick, synth, master;
    kick.setTrackNameOverride ("Kick");
    setParam (kick, "role", kick.parameters.getParameter ("role")->convertTo0to1 ((float) aimix::TrackRole::Kick));
    synth.setTrackNameOverride ("Hot Synth");
    setParam (synth, "role", synth.parameters.getParameter ("role")->convertTo0to1 ((float) aimix::TrackRole::Synth));
    setParam (master, "mode", 1.0f);

    FakePlayHead head;
    for (auto* p : { &kick, &synth, &master })
    {
        p->setPlayHead (&head);
        p->setPlayConfigDetails (2, 2, 48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        p->syncModeNow();
    }
    check (master.getLatestReport() != nullptr, "master instance started its engine");
    check (kick.getBusSlot() >= 0 && synth.getBusSlot() >= 0 && kick.getBusSlot() != synth.getBusSlot(), "listeners claimed distinct bus slots");

    juce::AudioBuffer<float> k (2, 512), s (2, 512), mix (2, 512);
    juce::MidiBuffer midi;
    const double fs = 48000.0;

    // ~4 s of audio, paced so the engine thread (15 Hz) keeps up, like real time would.
    for (int block = 0; block < 400; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (head.position + i) / fs;
            const double kt = std::fmod (t, 0.5);
            const float kv = (float) (0.8 * std::exp (-kt / 0.15) * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * kt));
            const float sv = std::fmod (t * 440.0, 1.0) < 0.5 ? 1.15f : -1.15f;
            for (int c = 0; c < 2; ++c) { k.setSample (c, i, kv); s.setSample (c, i, sv); mix.setSample (c, i, 0.5f * (kv + sv)); }
        }
        kick.processBlock (k, midi);
        synth.processBlock (s, midi);
        master.processBlock (mix, midi);
        head.position += 512;
        if (block % 4 == 3)
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (300));

    auto report = master.getLatestReport();
    check (report != nullptr && report->isActiveMaster, "master owns the bus");
    bool sawKick = false, sawSynth = false;
    if (report != nullptr)
        for (const auto& t : report->tracks)
        {
            sawKick |= t.view.name == "Kick" && t.view.role == aimix::TrackRole::Kick;
            sawSynth |= t.view.name == "Hot Synth";
        }
    check (sawKick && sawSynth, "master sees both listener instances with names and roles");
    check (report != nullptr && report->hasMaster, "master publishes its own mix-bus analysis");

    bool clipping = false;
    if (report != nullptr)
        for (const auto& sug : report->suggestions)
            clipping |= sug.ruleId == "gain.clipping" && ! sug.trackNames.empty() && sug.trackNames[0] == "Hot Synth";
    check (clipping, "clipping on the hot synth reaches the master as a suggestion");

    // Listener editor snapshot from the real processor.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (kick.createEditor());
        auto* ed = static_cast<AIMixEditor*> (editor.get());
        ed->setLibraryOverride (demoLibrary());
        ed->refresh();
        savePng (*editor, outDir.getChildFile ("ui_listener.png"));
        const auto& chain = ed->getListenerView().getChainPanel().getRecommendation();
        check (chain.role == aimix::TrackRole::Kick && chain.missingCount == 0 && chain.analogCount >= 2,
               "the kick's window suggests a full chain from the plugins found, analog first");

        setParam (kick, "role", kick.parameters.getParameter ("role")->convertTo0to1 ((float) aimix::TrackRole::Vocal));
        kick.setTrackNameOverride ("Lead Vox");
        ed->refresh();
        for (int i = 0; i < 15; ++i)
            ed->refresh();
        editor->setSize (1400, 1300);
        savePng (*editor, outDir.getChildFile ("ui_listener_vocal_chain.png"));
        check (ed->getListenerView().getChainPanel().getRecommendation().role == aimix::TrackRole::Vocal,
               "changing the role changes the suggested chain");
    }

    for (auto* p : { &kick, &synth, &master })
        p->setPlayHead (nullptr);
    return report;
}

juce::TextButton* findButton (juce::Component& root, const juce::String& text)
{
    for (auto* child : root.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == text)
            return b;
        if (auto* found = findButton (*child, text))
            return found;
    }
    return nullptr;
}

void renderMasterSnapshot (const juce::File& outDir)
{
    // Full synthetic session through the real pipeline, shown in the real editor.
    auto run = aimix::runSyntheticSession (10.0);
    auto report = run.engine->getLatestReport();

    AIMixProcessor p;
    setParam (p, "mode", 1.0f);
    p.syncModeNow();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* ed = static_cast<AIMixEditor*> (editor.get());
    ed->setReportOverride (report);
    ed->refresh();
    savePng (*editor, outDir.getChildFile ("ui_master.png"));

    editor->setSize (960, 620);   // smallest supported size
    ed->refresh();
    savePng (*editor, outDir.getChildFile ("ui_master_small.png"));

    // Tracks on "Auto": roles detected by the engine; one card shown as resolving.
    {
        aimix::SessionOptions options;
        options.autoRoles = true;
        auto autoRun = aimix::runSyntheticSession (10.0, 512, {}, 0, options);
        auto copy = std::make_shared<aimix::MixReport> (*autoRun.engine->getLatestReport());
        if (copy->suggestions.size() > 1)
            copy->suggestions[1].resolving = true;
        editor->setSize (1400, 900);
        ed->setReportOverride (copy);
        ed->refresh();
        savePng (*editor, outDir.getChildFile ("ui_master_auto_roles.png"));

        // The advice enlarged to the whole window.
        if (auto* enlarge = findButton (*editor, "Enlarge"))
        {
            enlarge->onClick();
            savePng (*editor, outDir.getChildFile ("ui_master_enlarged.png"));
            // Tall window: the whole step-by-step list at once.
            editor->setSize (1400, 1700);
            ed->refresh();
            savePng (*editor, outDir.getChildFile ("ui_master_steps.png"));
            editor->setSize (1400, 900);
            enlarge->onClick();
        }
        check (findButton (*editor, "Enlarge") != nullptr, "advice panel can be enlarged and shrunk again");
    }

    // Only the mix-bus instance: what a single plugin on the master shows.
    {
        aimix::SessionOptions options;
        options.masterOnly = true;
        auto busRun = aimix::runSyntheticSession (10.0, 512, {}, 0, options);
        ed->setReportOverride (busRun.engine->getLatestReport());
        ed->refresh();
        savePng (*editor, outDir.getChildFile ("ui_master_bus_only.png"));

        // Enlarged and tall: every workflow step at once.
        if (auto* enlarge = findButton (*editor, "Enlarge"))
        {
            enlarge->onClick();
            editor->setSize (1400, 1700);
            ed->refresh();
            savePng (*editor, outDir.getChildFile ("ui_master_bus_only_steps.png"));
            enlarge->onClick();
            editor->setSize (1400, 900);
        }
    }

    // The mix bus chain tab.
    {
        ed->setReportOverride (report);
        ed->setLibraryOverride (demoLibrary());
        ed->refresh();
        ed->getMasterView().showChain (true);
        savePng (*editor, outDir.getChildFile ("ui_master_chain.png"));
        const auto& chain = ed->getMasterView().getChainPanel().getRecommendation();
        check (chain.master && chain.slots.size() == 4 && chain.slots[0].pickFamily == "SSL"
                   && chain.slots[3].pickName.find ("Pro-L") != std::string::npos,
               "the master window suggests bus comp, program EQ, tape and a clean limiter");
        ed->getMasterView().showChain (false);
    }

    juce::FileOutputStream json (outDir.getChildFile ("synthetic_session_suggestions.json"));
    json.setPosition (0);
    json.truncate();
    json.writeText (aimix::toJson (report->suggestions), false, false, nullptr);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    checkLayouts();
    checkNonDestructiveAndRealtime();
    checkState();
    checkPluginScanner();
    checkMultiInstance (outDir);
    renderMasterSnapshot (outDir);

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
