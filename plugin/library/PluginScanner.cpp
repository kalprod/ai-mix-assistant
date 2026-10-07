#include "PluginScanner.h"

namespace aimix::library
{
namespace
{
bool cancelled (const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

bool isOwnPlugin (const juce::String& name) { return name.containsIgnoreCase ("AI Mix Assistant"); }

// The <string> after <key>name</key> in an Info.plist.
juce::String plistString (const juce::String& plist, const char* key)
{
    const auto k = plist.indexOf (juce::String ("<key>") + key + "</key>");
    if (k < 0)
        return {};
    const auto start = plist.indexOf (k, "<string>");
    const auto end = plist.indexOf (start, "</string>");
    if (start < 0 || end < 0)
        return {};
    return plist.substring (start + 8, end).trim();
}

// "com.fabfilter.Pro-Q.3" -> "FabFilter"-ish: the second part of the bundle id.
juce::String vendorFromBundleId (const juce::String& id)
{
    juce::StringArray parts;
    parts.addTokens (id, ".", {});
    if (parts.size() < 2)
        return {};
    auto v = parts[1];
    return v.substring (0, 1).toUpperCase() + v.substring (1);
}

int64_t modifiedMs (const juce::File& f) { return f.getLastModificationTime().toMilliseconds(); }

PluginInfo base (const juce::File& f, const char* format, const juce::String& name)
{
    PluginInfo p;
    p.format = format;
    p.name = name.toStdString();
    p.path = f.getFullPathName().toStdString();
    p.modifiedMs = modifiedMs (f);
    return p;
}

juce::String readModuleInfo (const juce::File& bundle)
{
    for (auto rel : { "Contents/Resources/moduleinfo.json", "Contents/moduleinfo.json" })
    {
        const auto f = bundle.getChildFile (rel);
        if (f.existsAsFile())
            return f.loadFileAsString();
    }
    return {};
}

// moduleinfo.json is JSON5: allow trailing commas before parsing it as JSON.
juce::var parseLooseJson (juce::String text)
{
    juce::String cleaned;
    cleaned.preallocateBytes ((size_t) text.length());
    auto p = text.getCharPointer();
    bool inString = false;
    while (! p.isEmpty())
    {
        const auto c = p.getAndAdvance();
        if (c == '"')
        {
            inString = ! inString;
        }
        else if (c == '\\' && inString && ! p.isEmpty())
        {
            cleaned << juce::String::charToString (c) << juce::String::charToString (p.getAndAdvance());
            continue;
        }
        else if (c == ',' && ! inString)
        {
            auto q = p;
            while (! q.isEmpty() && juce::CharacterFunctions::isWhitespace (*q))
                ++q;
            if (*q == '}' || *q == ']')
                continue;
        }
        cleaned << juce::String::charToString (c);
    }
    return juce::JSON::parse (cleaned);
}

bool insideBundle (const juce::File& f, const char* extension)
{
    for (auto d = f.getParentDirectory(); d != d.getParentDirectory(); d = d.getParentDirectory())
        if (d.hasFileExtension (extension))
            return true;
    return false;
}

void addFolder (juce::Array<juce::File>& list, const juce::File& f)
{
    if (f.isDirectory())
        list.addIfNotAlreadyThere (f);
}
} // namespace

juce::Array<juce::File> defaultVst3Folders()
{
    juce::Array<juce::File> out;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #if JUCE_MAC
    addFolder (out, juce::File ("/Library/Audio/Plug-Ins/VST3"));
    addFolder (out, home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
   #elif JUCE_WINDOWS
    addFolder (out, juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
    addFolder (out, juce::File (juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files")).getChildFile ("VST3"));
    addFolder (out, juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile ("Local/Programs/Common/VST3"));
   #else
    addFolder (out, home.getChildFile (".vst3"));
    addFolder (out, juce::File ("/usr/lib/vst3"));
    addFolder (out, juce::File ("/usr/local/lib/vst3"));
   #endif
    return out;
}

juce::Array<juce::File> defaultVst2Folders()
{
    juce::Array<juce::File> out;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #if JUCE_MAC
    addFolder (out, juce::File ("/Library/Audio/Plug-Ins/VST"));
    addFolder (out, home.getChildFile ("Library/Audio/Plug-Ins/VST"));
   #elif JUCE_WINDOWS
    const auto programFiles = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    addFolder (out, programFiles.getChildFile ("VSTPlugins"));
    addFolder (out, programFiles.getChildFile ("Steinberg/VSTPlugins"));
    addFolder (out, programFiles.getChildFile ("Common Files/VST2"));
   #else
    addFolder (out, home.getChildFile (".vst"));
    addFolder (out, juce::File ("/usr/lib/vst"));
    addFolder (out, juce::File ("/usr/local/lib/vst"));
   #endif
    return out;
}

std::vector<PluginInfo> describeVst3 (const juce::File& bundle)
{
    std::vector<PluginInfo> out;
    const auto fileName = bundle.getFileNameWithoutExtension();

    const auto json = readModuleInfo (bundle);
    if (json.isNotEmpty())
    {
        const auto info = parseLooseJson (json);
        const auto factoryVendor = info.getProperty ("Factory Info", {}).getProperty ("Vendor", {}).toString();
        if (auto* classes = info.getProperty ("Classes", {}).getArray())
        {
            for (const auto& c : *classes)
            {
                if (c.getProperty ("Category", {}).toString() != "Audio Module Class")
                    continue;
                juce::StringArray subs;
                if (auto* s = c.getProperty ("Sub Categories", {}).getArray())
                    for (const auto& v : *s)
                        subs.add (v.toString());
                if (subs.contains ("Instrument") || subs.contains ("Generator"))
                    continue;
                const auto name = c.getProperty ("Name", fileName).toString();
                if (isOwnPlugin (name))
                    continue;
                auto p = base (bundle, "VST3", name);
                auto vendor = c.getProperty ("Vendor", factoryVendor).toString();
                p.vendor = (vendor.isNotEmpty() ? vendor : factoryVendor).toStdString();
                p.version = c.getProperty ("Version", {}).toString().toStdString();
                p.reportedCategory = subs.joinIntoString ("|").toStdString();
                p.id = "VST3:" + c.getProperty ("CID", name).toString().toStdString();
                out.push_back (std::move (p));
            }
            if (! out.empty() || classes->size() > 0)
                return out;
        }
    }

    if (isOwnPlugin (fileName))
        return out;

    auto p = base (bundle, "VST3", fileName);
    const auto plist = bundle.getChildFile ("Contents/Info.plist");
    if (plist.existsAsFile())
    {
        const auto text = plist.loadFileAsString();
        const auto name = plistString (text, "CFBundleName");
        if (name.isNotEmpty() && ! name.equalsIgnoreCase ("VST3") && ! isOwnPlugin (name))
            p.name = name.toStdString();
        p.version = plistString (text, "CFBundleShortVersionString").toStdString();
        p.vendor = vendorFromBundleId (plistString (text, "CFBundleIdentifier")).toStdString();
    }
    p.id = "VST3:" + p.path;
    out.push_back (std::move (p));
    return out;
}

std::vector<PluginInfo> scanInstalledPlugins (const ScanOptions& options, const std::atomic<bool>* cancel)
{
    std::vector<PluginInfo> out;

   #if JUCE_MAC
    if (options.audioUnits)
        out = scanAudioUnits (cancel);
   #endif

    if (options.vst3)
    {
        auto folders = defaultVst3Folders();
        folders.addArray (options.extraVst3Folders);
        for (const auto& folder : folders)
        {
            // Bundles are folders (macOS, newer Windows/Linux); older Windows ones are single files.
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.vst3",
                                                                    juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
            {
                if (cancelled (cancel))
                    return out;
                const auto f = entry.getFile();
                if (insideBundle (f, "vst3"))
                    continue;   // a file inside a bundle, not a plugin
                for (auto& p : describeVst3 (f))
                    out.push_back (std::move (p));
            }
        }
    }

    if (options.vst2)
    {
       #if JUCE_MAC
        const char* pattern = "*.vst";
       #elif JUCE_WINDOWS
        const char* pattern = "*.dll";
       #else
        const char* pattern = "*.so";
       #endif
        for (const auto& folder : defaultVst2Folders())
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, pattern,
                                                                    juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
            {
                if (cancelled (cancel))
                    return out;
                const auto f = entry.getFile();
                if (insideBundle (f, "vst") || insideBundle (f, "vst3") || isOwnPlugin (f.getFileName()))
                    continue;
                auto p = base (f, "VST", f.getFileNameWithoutExtension());
                p.id = "VST:" + p.path;
                out.push_back (std::move (p));
            }
    }

    // Duplicate VST3 shells (the same class found twice) only once.
    std::sort (out.begin(), out.end(), [] (const PluginInfo& a, const PluginInfo& b) { return a.id < b.id; });
    out.erase (std::unique (out.begin(), out.end(), [] (const PluginInfo& a, const PluginInfo& b) { return a.id == b.id; }), out.end());

    for (auto& p : out)
        tagPlugin (p);
    return out;
}

} // namespace aimix::library
