#include "Json.h"

#include "core/theory/ChordNamer.h"
#include "core/theory/RomanNumeral.h"

namespace so::bridge::json
{

juce::String str (const std::string& utf8) { return juce::String::fromUTF8 (utf8.c_str()); }

juce::String symbol (const theory::Chord& chord) { return str (theory::ChordNamer::name (chord)); }

juce::String fn (theory::HarmonicFunction f)
{
    switch (f)
    {
        case theory::HarmonicFunction::Tonic:       return "tonic";
        case theory::HarmonicFunction::Predominant: return "predominant";
        case theory::HarmonicFunction::Dominant:    return "dominant";
        case theory::HarmonicFunction::Other:       break;
    }
    return "color";
}

juce::String keyName (const theory::Key& key) { return str (key.toString()); }

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : props)
        o->setProperty (k, v);
    return juce::var (o);
}

juce::var ack (bool ok, const juce::String& error)
{
    return error.isEmpty() ? object ({ { "ok", ok } }) : object ({ { "ok", ok }, { "error", error } });
}

juce::String repairUtf8 (const juce::String& s)
{
    std::string bytes;
    bool high = false;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c > 0xff)
            return s;               // real Unicode beyond Latin-1: already decoded correctly
        high = high || c >= 0x80;
        bytes.push_back ((char) (unsigned char) c);
    }
    if (! high || ! juce::CharPointer_UTF8::isValidString (bytes.c_str(), (int) bytes.size()))
        return s;
    return juce::String::fromUTF8 (bytes.c_str(), (int) bytes.size());
}

juce::var repair (const juce::var& v)
{
    if (v.isString())
        return repairUtf8 (v.toString());

    if (const auto* arr = v.getArray())
    {
        juce::Array<juce::var> out;
        for (const auto& item : *arr)
            out.add (repair (item));
        return out;
    }

    if (auto* obj = v.getDynamicObject())
    {
        auto* copy = new juce::DynamicObject();
        for (const auto& prop : obj->getProperties())
            copy->setProperty (prop.name, repair (prop.value));
        return juce::var (copy);
    }

    return v;
}

juce::var analysis (const analysis::AnalysisResult& a, const juce::String& source, const juce::String& sourceName)
{
    juce::Array<juce::var> segments;
    for (const auto& s : a.segments)
    {
        const auto rn = theory::RomanNumeral::analyze (s.chord, a.key);

        juce::Array<juce::var> alts;
        for (const auto& alt : s.alternatives)
            alts.add (object ({ { "symbol", symbol (alt.chord) }, { "confidence", alt.confidence } }));

        segments.add (object ({
            { "id", juce::String (s.id) },
            { "startBeat", s.startBeat },
            { "beats", s.beats },
            { "symbol", symbol (s.chord) },
            { "confidence", s.confidence },
            { "roman", str (rn.numeral) },
            { "fn", fn (rn.function) },
            { "alternatives", alts },
            { "userLocked", s.userLocked },
            { "userCorrected", s.userCorrected },
        }));
    }

    juce::Array<juce::var> melody;
    for (const auto& m : a.melody)
        melody.add (object ({ { "pitch", m.pitch }, { "startBeat", m.startBeat }, { "beats", m.beats } }));

    return object ({
        { "schemaVersion", 1 },
        { "source", source },
        { "sourceName", sourceName },
        { "bars", a.bars },
        { "bpm", a.grid.bpm },
        { "beatsPerBar", a.grid.beatsPerBar },
        { "downbeatSec", a.grid.downbeatSec },
        { "key", keyName (a.key) },
        { "keyConfidence", a.keyConfidence },
        { "segments", segments },
        { "melody", melody },
    });
}

static juce::String techniqueName (reharm::Technique t)
{
    using T = reharm::Technique;
    switch (reharm::familyOf (t))
    {
        case reharm::Family::Borrowed:         return "Borrowed";
        case reharm::Family::Secondary:        return "Secondary";
        case reharm::Family::TritoneSub:       return "Tritone sub";
        case reharm::Family::ChromaticMediant: return "Chromatic mediant";
        case reharm::Family::NeoRiemannian:    return "Neo-Riemannian";
        case reharm::Family::Diatonic:         break;
    }
    juce::ignoreUnused (T::Original);
    return "Diatonic";
}

juce::var suggestions (const std::vector<reharm::Suggestion>& list, std::uint64_t seed)
{
    juce::Array<juce::var> out;

    for (const auto& sg : list)
    {
        juce::StringArray techniques;
        for (auto t : sg.techniques)
            if (t != reharm::Technique::Original)
                techniques.addIfNotAlreadyThere (techniqueName (t));
        if (techniques.isEmpty())
            techniques.add ("Diatonic");

        juce::Array<juce::var> tech;
        for (const auto& t : techniques)
            tech.add (t);

        juce::Array<juce::var> chords;
        int i = 0;
        for (const auto& c : sg.chords)
        {
            auto chord = object ({
                { "id", str (sg.id) + "-" + juce::String (i++) },
                { "startBeat", c.startBeat },
                { "beats", c.beats },
                { "symbol", str (c.symbol) },
                { "roman", str (c.roman) },
                { "fn", fn (c.function) },
                { "changed", c.changed },
            });

            if (c.changed && ! c.explanation.simple.empty())
            {
                const auto& e = c.explanation;
                // The UI prints "plain (term)", so drop the term from the sentence if the explainer
                // already wrote it in parentheses.
                auto plain = str (e.simple);
                const auto term = str (e.techniqueTerm);
                if (term.isNotEmpty())
                    plain = plain.replace (" (" + term + ")", "");
                auto why = object ({ { "plain", plain }, { "term", term } });
                if (! e.detailed.empty())
                    why.getDynamicObject()->setProperty ("detail", str (e.detailed));
                chord.getDynamicObject()->setProperty ("why", why);
            }
            chords.add (chord);
        }

        out.add (object ({
            { "schemaVersion", 1 },
            { "id", str (sg.id) },
            { "title", str (sg.title) },
            { "techniques", tech },
            { "tension", sg.tension },
            { "movement", sg.movement },
            { "summary", str (sg.summary) },
            { "chords", chords },
        }));
    }

    return object ({ { "schemaVersion", 1 }, { "seed", (juce::int64) seed }, { "suggestions", out } });
}

} // namespace so::bridge::json
