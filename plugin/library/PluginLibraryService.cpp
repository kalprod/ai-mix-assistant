#include "PluginLibraryService.h"

namespace aimix::library
{
PluginLibraryService::PluginLibraryService() : juce::Thread ("K MASTER plugin scan")
{
    auto file = libraryFile();
    if (! file.existsAsFile())   // saved before the rename
        file = file.getParentDirectory().getSiblingFile ("AI Mix Assistant").getChildFile (file.getFileName());
    if (file.existsAsFile())
    {
        auto loaded = fromJson (juce::JSON::parse (file), &scannedAtMs);
        std::lock_guard<std::mutex> g (lock);
        plugins = std::move (loaded);
    }
    else
    {
        rescan();   // first time: find what's installed
    }
}

PluginLibraryService::~PluginLibraryService()
{
    cancel = true;
    stopThread (10000);
    cancelPendingUpdate();
}

std::vector<PluginInfo> PluginLibraryService::getPlugins() const
{
    std::lock_guard<std::mutex> g (lock);
    return plugins;
}

bool PluginLibraryService::hasScanned() const
{
    std::lock_guard<std::mutex> g (lock);
    return scannedAtMs != 0;
}

juce::String PluginLibraryService::getStatusText() const
{
    if (isScanning())
        return "Looking for your plugins...";
    std::lock_guard<std::mutex> g (lock);
    if (scannedAtMs == 0)
        return "Plugins not scanned yet";
    int analog = 0;
    for (const auto& p : plugins)
        if (p.character == PluginCharacter::Analog)
            ++analog;
    return "Found " + juce::String ((int) plugins.size()) + " plugins, " + juce::String (analog) + " analog models";
}

void PluginLibraryService::rescan()
{
    if (isThreadRunning())
        return;
    cancel = false;
    startThread (juce::Thread::Priority::low);
    triggerAsyncUpdate();   // show "looking for your plugins"
}

void PluginLibraryService::run()
{
    auto fresh = scanInstalledPlugins ({}, &cancel);
    if (cancel)
        return;

    const auto now = juce::Time::currentTimeMillis();
    {
        std::lock_guard<std::mutex> g (lock);
        keepUserChanges (fresh, plugins);
        plugins = std::move (fresh);
        scannedAtMs = now;
    }

    const auto file = libraryFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (toJson (getPlugins(), now)));
    triggerAsyncUpdate();
}

void PluginLibraryService::handleAsyncUpdate()
{
    changes.sendChangeMessage();
}

juce::File PluginLibraryService::libraryFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("K MASTER").getChildFile ("plugin-library.json");
}

juce::var PluginLibraryService::toJson (const std::vector<PluginInfo>& list, int64_t scannedAt)
{
    juce::Array<juce::var> items;
    for (const auto& p : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (p.id));
        o->setProperty ("name", juce::String (p.name));
        o->setProperty ("vendor", juce::String (p.vendor));
        o->setProperty ("format", juce::String (p.format));
        o->setProperty ("path", juce::String (p.path));
        o->setProperty ("version", juce::String (p.version));
        o->setProperty ("category", juce::String (p.reportedCategory));
        o->setProperty ("modified", (juce::int64) p.modifiedMs);
        o->setProperty ("functions", juce::String (functionsToString (p.functions)));
        o->setProperty ("subtype", juce::String (p.subtype));
        o->setProperty ("character", juce::String (toString (p.character)));
        o->setProperty ("family", juce::String (p.family));
        juce::Array<juce::var> emulates;
        for (const auto& e : p.emulates)
            emulates.add (juce::String (e));
        o->setProperty ("emulates", emulates);
        o->setProperty ("tagSource", juce::String (toString (p.tagSource)));
        o->setProperty ("confidence", p.tagConfidence);
        o->setProperty ("favourite", p.favourite);
        juce::Array<juce::var> presets;
        for (const auto& name : p.presets)
            presets.add (juce::String (name));
        o->setProperty ("presets", presets);
        items.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", 1);
    root->setProperty ("scannedAt", (juce::int64) scannedAt);
    root->setProperty ("plugins", items);
    return juce::var (root);
}

std::vector<PluginInfo> PluginLibraryService::fromJson (const juce::var& json, int64_t* scannedAt)
{
    std::vector<PluginInfo> out;
    if (scannedAt != nullptr)
        *scannedAt = (int64_t) (juce::int64) json.getProperty ("scannedAt", 0);
    auto* items = json.getProperty ("plugins", {}).getArray();
    if (items == nullptr || (int) json.getProperty ("schema", 0) != 1)
        return out;

    for (const auto& o : *items)
    {
        PluginInfo p;
        auto str = [&o] (const char* key) { return o.getProperty (key, {}).toString().toStdString(); };
        p.id = str ("id");
        p.name = str ("name");
        p.vendor = str ("vendor");
        p.format = str ("format");
        p.path = str ("path");
        p.version = str ("version");
        p.reportedCategory = str ("category");
        p.modifiedMs = (int64_t) (juce::int64) o.getProperty ("modified", 0);
        p.functions = functionsFromString (str ("functions"));
        p.subtype = str ("subtype");
        p.character = characterFromString (str ("character"));
        p.family = str ("family");
        if (auto* e = o.getProperty ("emulates", {}).getArray())
            for (const auto& v : *e)
                p.emulates.push_back (v.toString().toStdString());
        const auto source = str ("tagSource");
        p.tagSource = source == "you" ? TagSource::User : source == "catalog" ? TagSource::Catalog
                    : source == "keywords" ? TagSource::Keywords : TagSource::Category;
        p.tagConfidence = (float) (double) o.getProperty ("confidence", 0.3);
        p.favourite = (bool) o.getProperty ("favourite", false);
        if (auto* pr = o.getProperty ("presets", {}).getArray())
            for (const auto& v : *pr)
                p.presets.push_back (v.toString().toStdString());
        if (! p.id.empty() && ! p.name.empty())
            out.push_back (std::move (p));
    }
    return out;
}

void PluginLibraryService::keepUserChanges (std::vector<PluginInfo>& fresh, const std::vector<PluginInfo>& old)
{
    for (auto& p : fresh)
    {
        auto it = std::find_if (old.begin(), old.end(), [&p] (const PluginInfo& o) { return o.id == p.id; });
        if (it == old.end())
            continue;
        p.favourite = it->favourite;
        if (it->tagSource == TagSource::User)
        {
            p.functions = it->functions;
            p.subtype = it->subtype;
            p.character = it->character;
            p.family = it->family;
            p.emulates = it->emulates;
            p.tagSource = TagSource::User;
            p.tagConfidence = 1.0f;
        }
    }
}

} // namespace aimix::library
