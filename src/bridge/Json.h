#pragma once

// C++ engine structs -> bridge JSON (docs/bridge-contract.md, schemaVersion 1).

#include "core/analysis/AnalysisTypes.h"
#include "core/reharm/Suggestion.h"

#include <juce_core/juce_core.h>

namespace so::bridge::json
{

juce::String str (const std::string& utf8);
juce::String symbol (const theory::Chord& chord);           // Pop, real accidentals
juce::String fn (theory::HarmonicFunction f);               // tonic | predominant | dominant | color
juce::String keyName (const theory::Key& key);              // "F minor"

juce::var analysis (const analysis::AnalysisResult& a, const juce::String& source, const juce::String& sourceName);
juce::var suggestions (const std::vector<reharm::Suggestion>& list, std::uint64_t seed);

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props);
juce::var ack (bool ok, const juce::String& error = {});

/** Some WebView backends hand JS strings to C++ as Latin-1 decoded UTF-8 ("A\u00e2\u0099\u00ad" for
    "A\u266d"). Re-decodes such strings; leaves correct strings untouched. Applied to every argument
    that arrives from the page, recursively through arrays and objects. */
juce::String repairUtf8 (const juce::String& s);
juce::var repair (const juce::var& v);

} // namespace so::bridge::json
