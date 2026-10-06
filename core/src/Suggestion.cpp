#include "aimix/Suggestion.h"

#include <cmath>
#include <cstdio>

namespace aimix
{
const char* toString (Severity s) noexcept
{
    switch (s)
    {
        case Severity::Info:     return "info";
        case Severity::Warning:  return "warning";
        case Severity::Critical: return "critical";
    }
    return "info";
}

const char* toString (Category c) noexcept
{
    switch (c)
    {
        case Category::Gain:    return "gain";
        case Category::Eq:      return "eq";
        case Category::Panning: return "panning";
        case Category::Phase:   return "phase";
        case Category::Dynamics: return "dynamics";
        case Category::Depth:   return "depth";
        case Category::General: return "general";
    }
    return "gain";
}

const char* toString (MixStep s) noexcept
{
    switch (s)
    {
        case MixStep::GainStaging: return "gain_staging";
        case MixStep::Eq:          return "eq";
        case MixStep::Dynamics:    return "dynamics";
        case MixStep::Stereo:      return "stereo";
        case MixStep::Depth:       return "depth";
        case MixStep::FinalTip:    return "final_tip";
    }
    return "gain_staging";
}

const char* stepTitle (MixStep s) noexcept
{
    switch (s)
    {
        case MixStep::GainStaging: return "Gain staging";
        case MixStep::Eq:          return "EQ";
        case MixStep::Dynamics:    return "Dynamics";
        case MixStep::Stereo:      return "Stereo placement";
        case MixStep::Depth:       return "Depth";
        case MixStep::FinalTip:    return "Final tip";
    }
    return "";
}

const char* stepSummary (MixStep s) noexcept
{
    switch (s)
    {
        case MixStep::GainStaging: return "Set levels first. Keep peaks between about -10 and 0 dB to avoid clipping and leave headroom.";
        case MixStep::Eq:          return "Remove unwanted frequencies. High-pass non-bass sounds and cut mud (around 250-500 Hz) for clarity.";
        case MixStep::Dynamics:    return "Compression controls levels. Slow attack keeps transients; parallel compression adds weight without squashing.";
        case MixStep::Stereo:      return "Centre bass and vocals, pan other elements outward, and always check the mix in mono.";
        case MixStep::Depth:       return "Create space without clutter: pre-delay on vocal reverb, EQ the reverb tails, try delay instead of reverb.";
        case MixStep::FinalTip:    return "Every track is different. Use this as a guide, trust your ears and tweak as needed.";
    }
    return "";
}

MixStep stepForRule (const std::string& id) noexcept
{
    auto starts = [&id] (const char* prefix) { return id.rfind (prefix, 0) == 0; };
    if (id == "mix.mono_compat" || id == "mix.too_wide" || starts ("pan.") || starts ("phase."))
        return MixStep::Stereo;
    if (starts ("eq."))    return MixStep::Eq;
    if (starts ("dyn."))   return MixStep::Dynamics;
    if (starts ("depth.")) return MixStep::Depth;
    if (starts ("tip."))   return MixStep::FinalTip;
    return MixStep::GainStaging;   // gain.*, mix.headroom, mix.loudness
}

const char* toString (ActionType a) noexcept
{
    switch (a)
    {
        case ActionType::None:           return "none";
        case ActionType::AdjustGain:     return "adjust_gain";
        case ActionType::EqBell:         return "eq_bell";
        case ActionType::HighPass:       return "high_pass";
        case ActionType::Pan:            return "pan";
        case ActionType::MonoBelow:      return "mono_below";
        case ActionType::TimeAlign:      return "time_align";
        case ActionType::InvertPolarity: return "invert_polarity";
    }
    return "none";
}

std::string formatFrequency (float hz)
{
    char buf[32];
    if (hz >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.1f kHz", hz / 1000.0f);
    else
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    return buf;
}

std::string describeAction (const SuggestedAction& a)
{
    char buf[128];
    switch (a.type)
    {
        case ActionType::AdjustGain:
            std::snprintf (buf, sizeof (buf), "Gain %+.1f dB", a.gainDb);
            return buf;
        case ActionType::EqBell:
            std::snprintf (buf, sizeof (buf), "EQ bell %+.1f dB @ %s, Q %.1f", a.gainDb, formatFrequency (a.frequencyHz).c_str(), a.q);
            return buf;
        case ActionType::HighPass:
            return "High-pass @ " + formatFrequency (a.frequencyHz);
        case ActionType::Pan:
            std::snprintf (buf, sizeof (buf), "Pan %s%.0f%%", a.pan < 0 ? "L " : (a.pan > 0 ? "R " : "C "), std::abs (a.pan) * 100.0f);
            return buf;
        case ActionType::MonoBelow:
            return "Mono below " + formatFrequency (a.frequencyHz);
        case ActionType::TimeAlign:
            std::snprintf (buf, sizeof (buf), "Delay %.1f samples (%.2f ms)", a.delaySamples, a.delayMs);
            return buf;
        case ActionType::InvertPolarity:
            return "Invert polarity";
        case ActionType::None:
            break;
    }
    return {};
}

static void appendEscaped (std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof (buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += (char) c;
                }
        }
    }
    out += '"';
}

static void appendNumber (std::string& out, double v)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.4g", std::isfinite (v) ? v : 0.0);
    out += buf;
}

std::string toJson (const Suggestion& s)
{
    std::string o = "{\"key\":";
    appendEscaped (o, s.key);
    o += ",\"rule\":";       appendEscaped (o, s.ruleId);
    o += ",\"category\":";   appendEscaped (o, toString (s.category));
    o += ",\"step\":";       appendNumber (o, (int) s.step);
    if (s.tip) o += ",\"tip\":true";
    o += ",\"severity\":";   appendEscaped (o, toString (s.severity));
    o += ",\"confidence\":"; appendNumber (o, s.confidence);

    o += ",\"tracks\":[";
    for (size_t i = 0; i < s.trackIds.size(); ++i)
    {
        if (i) o += ',';
        o += "{\"id\":";
        appendNumber (o, s.trackIds[i]);
        o += ",\"name\":";
        appendEscaped (o, i < s.trackNames.size() ? s.trackNames[i] : std::string());
        o += '}';
    }
    o += "],\"title\":"; appendEscaped (o, s.title);
    o += ",\"detail\":"; appendEscaped (o, s.detail);
    o += ",\"steps\":[";
    for (size_t i = 0; i < s.steps.size(); ++i)
    {
        if (i) o += ',';
        appendEscaped (o, s.steps[i]);
    }
    o += "],\"action\":{\"type\":";
    appendEscaped (o, toString (s.action.type));
    o += ",\"track\":";        appendNumber (o, s.action.trackId);
    o += ",\"gain_db\":";      appendNumber (o, s.action.gainDb);
    o += ",\"frequency_hz\":"; appendNumber (o, s.action.frequencyHz);
    o += ",\"q\":";            appendNumber (o, s.action.q);
    o += ",\"pan\":";          appendNumber (o, s.action.pan);
    o += ",\"delay_samples\":"; appendNumber (o, s.action.delaySamples);
    o += ",\"delay_ms\":";     appendNumber (o, s.action.delayMs);
    o += ",\"summary\":";      appendEscaped (o, describeAction (s.action));
    o += "}}";
    return o;
}

std::string toJson (const std::vector<Suggestion>& list)
{
    std::string o = "[";
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i) o += ',';
        o += toJson (list[i]);
    }
    o += ']';
    return o;
}

} // namespace aimix
