#include "PluginScanner.h"

#include <AudioToolbox/AudioToolbox.h>

namespace aimix::library
{
namespace
{
juce::String osType (OSType t)
{
    const char c[] = { (char) (t >> 24), (char) (t >> 16), (char) (t >> 8), (char) t, 0 };
    return juce::String (c);
}
}

// Lists effect Audio Units from the system registry. Nothing is instantiated:
// the registry already has the name, maker and version.
std::vector<PluginInfo> scanAudioUnits (const std::atomic<bool>* cancel)
{
    std::vector<PluginInfo> out;

    for (OSType type : { kAudioUnitType_Effect, kAudioUnitType_MusicEffect })
    {
        AudioComponentDescription filter {};
        filter.componentType = type;
        AudioComponent comp = nullptr;

        while ((comp = AudioComponentFindNext (comp, &filter)) != nullptr)
        {
            if (cancel != nullptr && cancel->load())
                return out;

            AudioComponentDescription desc {};
            if (AudioComponentGetDescription (comp, &desc) != noErr)
                continue;
            if (desc.componentManufacturer == 'Aimx')
                continue;   // ourselves

            juce::String name, vendor;
            CFStringRef cfName = nullptr;
            if (AudioComponentCopyName (comp, &cfName) == noErr && cfName != nullptr)
            {
                name = juce::String::fromCFString (cfName);
                CFRelease (cfName);
            }
            if (name.containsChar (':'))
            {
                vendor = name.upToFirstOccurrenceOf (":", false, false).trim();
                name = name.fromFirstOccurrenceOf (":", false, false).trim();
            }
            if (name.isEmpty() || name.containsIgnoreCase ("AI Mix Assistant"))
                continue;

            PluginInfo p;
            p.format = "AU";
            p.name = name.toStdString();
            p.vendor = vendor.toStdString();
            // Same identifier JUCE's AudioUnitPluginFormat uses, so the chain can load it later.
            p.id = ("AudioUnit:Effects/" + osType (desc.componentType) + "," + osType (desc.componentSubType)
                    + "," + osType (desc.componentManufacturer)).toStdString();
            p.path = p.id;
            UInt32 v = 0;
            if (AudioComponentGetVersion (comp, &v) == noErr)
                p.version = (juce::String ((int) (v >> 16)) + "." + juce::String ((int) ((v >> 8) & 0xff))
                             + "." + juce::String ((int) (v & 0xff))).toStdString();
            p.reportedCategory = type == kAudioUnitType_MusicEffect ? "MusicEffect" : "Effect";
            out.push_back (std::move (p));
        }
    }
    return out;
}

} // namespace aimix::library
