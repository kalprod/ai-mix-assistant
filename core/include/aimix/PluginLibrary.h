#pragma once

// What we know about the plugins installed on this machine, and how each one
// is tagged: what it does (EQ, compressor, tape...), what kind it is (opto,
// FET, program EQ...), and whether it models analog hardware.
//
// Scanning (finding plugins on disk and in the OS registry) lives in the
// plugin layer; this part only turns the raw facts a scan finds into tags, so
// it can be tested without any plugins installed.

#include <cstdint>
#include <string>
#include <vector>

namespace aimix
{
// What a plugin does. A channel strip does several things at once.
enum PluginFunction : uint32_t
{
    kFnEq           = 1u << 0,
    kFnCompressor   = 1u << 1,
    kFnLimiter      = 1u << 2,
    kFnSaturation   = 1u << 3,
    kFnTape         = 1u << 4,
    kFnPreamp       = 1u << 5,
    kFnChannelStrip = 1u << 6,
    kFnDeesser      = 1u << 7,
    kFnTransient    = 1u << 8,
    kFnReverb       = 1u << 9,
    kFnDelay        = 1u << 10,
    kFnModulation   = 1u << 11,
    kFnMultiband    = 1u << 12,
    kFnUtility      = 1u << 13,
};
constexpr int kNumPluginFunctions = 14;

enum class PluginCharacter : uint8_t
{
    Digital = 0,       // clean, transparent
    AnalogInspired,    // analog colour, no specific unit
    Analog,            // models a specific piece of hardware
};

enum class TagSource : uint8_t { Category = 0, Keywords, Catalog, User };

struct PluginInfo
{
    // Facts from the scan.
    std::string id;                 // "<format>:<identifier>", stable across scans
    std::string name;
    std::string vendor;
    std::string format;             // "AU", "VST3", "VST"
    std::string path;
    std::string version;
    std::string reportedCategory;   // the plugin's own category, e.g. "Fx|EQ"
    int64_t modifiedMs = 0;

    // Tags.
    uint32_t functions = 0;         // PluginFunction bits
    std::string subtype;            // "opto", "fet", "vca", "vari_mu", "console", "program_eq", "parametric", "dynamic", "precision"
    PluginCharacter character = PluginCharacter::Digital;
    std::string family;             // hardware family: "Neve", "SSL", "Pultec", "1176", ...
    std::vector<std::string> emulates;
    TagSource tagSource = TagSource::Category;
    float tagConfidence = 0.3f;
    bool favourite = false;

    // Preset names saved on disk for this plugin (user and some factory presets).
    std::vector<std::string> presets;

    bool does (PluginFunction f) const noexcept { return (functions & f) != 0; }
};

// Fills the tag fields of `p` from its name, vendor and reported category.
// Order: built-in catalog, hardware words in the name, colour words, then the
// reported category. Leaves user overrides to the caller.
void tagPlugin (PluginInfo& p);

// Lower-case words of a plugin name, splitting CamelCase and digits:
// "UADx Pultec EQP-1A" -> { "uadx", "pultec", "eqp", "1a" }.
std::vector<std::string> nameTokens (const std::string& s);

const char* toString (PluginCharacter) noexcept;    // "analog", "analog_inspired", "digital"
const char* toString (TagSource) noexcept;          // "catalog", ...
const char* functionName (PluginFunction) noexcept; // "eq", "compressor", ...
std::string functionsToString (uint32_t functions); // "eq,compressor"
uint32_t functionsFromString (const std::string& s);
PluginCharacter characterFromString (const std::string& s) noexcept;

} // namespace aimix
