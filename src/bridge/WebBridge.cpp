#include "WebBridge.h"

#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"
#include "plugin/PluginProcessor.h"
#include "services/SessionEngine.h"
#include "bridge/Json.h"

namespace so::bridge
{
namespace json = so::bridge::json;

using juce::var;
using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

namespace
{
    var makeObject (std::initializer_list<std::pair<const char*, var>> props)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : props)
            o->setProperty (k, v);
        return var (o);
    }

    var parseChord (const juce::String& symbol)
    {
        using namespace so::theory;
        const auto result = ChordSymbolParser::parse (symbol.toStdString());

        if (const auto* err = std::get_if<ParseError> (&result))
            return makeObject ({ { "ok", false }, { "error", juce::String::fromUTF8 (err->message.c_str()) } });

        const auto& chord = std::get<Chord> (result);

        NamingOptions pop;
        NamingOptions jazz;
        jazz.style = NamingOptions::Style::Jazz;

        juce::Array<var> notes;
        for (const auto& sp : chord.chordToneSpellings())
            notes.add (juce::String::fromUTF8 (sp.toString().c_str()));

        return makeObject ({
            { "ok", true },
            { "symbol", juce::String::fromUTF8 (ChordNamer::name (chord, pop).c_str()) },
            { "pop", juce::String::fromUTF8 (ChordNamer::name (chord, pop).c_str()) },
            { "jazz", juce::String::fromUTF8 (ChordNamer::name (chord, jazz).c_str()) },
            { "notes", notes },
            { "bass", juce::String::fromUTF8 ((chord.bass ? chord.bass->toString() : chord.root.toString()).c_str()) },
            { "slash", chord.isSlash() },
        });
    }

    var setProgression (SecondOpinionProcessor& p, const var& payload)
    {
        SecondOpinionProcessor::Progression prog;
        prog.id = payload.getProperty ("id", {}).toString();
        prog.title = payload.getProperty ("title", "Suggestion").toString();
        prog.bpm = juce::jlimit (20.0, 400.0, (double) payload.getProperty ("bpm", 120.0));

        double beat = 0.0;
        juce::Array<var> errors;

        if (const auto* chords = payload.getProperty ("chords", {}).getArray())
        {
            for (const auto& c : *chords)
            {
                const auto symbol = c.getProperty ("symbol", {}).toString();
                const double beats = juce::jlimit (0.25, 64.0, (double) c.getProperty ("beats", 4.0));

                if (auto chord = so::theory::ChordSymbolParser::tryParse (symbol.toStdString()))
                    prog.chords.push_back ({ *chord, beat, beats });
                else
                    errors.add (symbol);

                beat += beats;
            }
        }

        const int count = (int) prog.chords.size();
        p.progression = std::move (prog);
        p.session.setProperty ("selectedSuggestion", p.progression.id, nullptr);

        if (p.onProgressionChanged)
            p.onProgressionChanged();

        return makeObject ({ { "ok", errors.isEmpty() }, { "count", count }, { "unparsed", errors } });
    }
}

juce::WebBrowserComponent::Options addNativeFunctions (juce::WebBrowserComponent::Options options,
                                                        SecondOpinionProcessor& processor,
                                                        std::function<void()> exportMidi)
{
    auto* proc = &processor;
    auto* eng = &processor.getEngine();
    auto arg = [] (const juce::Array<var>& a, int i) { return i < a.size() ? json::repair (a[i]) : var(); };

    // Every function of docs/bridge-contract.md. Long work acknowledges at once; results arrive as events.
    options = options
        .withNativeFunction ("startCapture", [eng] (const juce::Array<var>&, Completion c) { c (eng->startCapture()); })
        .withNativeFunction ("stopCapture", [eng] (const juce::Array<var>&, Completion c) { c (eng->stopCapture()); })
        .withNativeFunction ("setTempo", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->setTempo ((double) arg (a, 0))); })
        .withNativeFunction ("openAudioFile", [eng] (const juce::Array<var>&, Completion c) { eng->openAudioFile (std::move (c)); })
        .withNativeFunction ("analyzePcm", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->analyzePcm (arg (a, 0))); })
        .withNativeFunction ("cancel", [eng] (const juce::Array<var>&, Completion c) { c (eng->cancel()); })
        .withNativeFunction ("setChord", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->setChord (arg (a, 0).toString(), arg (a, 1).toString())); })
        .withNativeFunction ("lock", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->lock (arg (a, 0), (bool) arg (a, 1))); })
        .withNativeFunction ("generate", [eng, arg] (const juce::Array<var>& a, Completion c)
        {
            const auto seed = (juce::int64) arg (a, 0).getProperty ("seed", 0);
            c (eng->generate ((std::uint64_t) juce::jmax ((juce::int64) 0, seed)));
        })
        .withNativeFunction ("audition", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->audition (arg (a, 0))); })
        .withNativeFunction ("buildSong", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->buildSong (arg (a, 0))); })
        .withNativeFunction ("chooseSectionOption", [eng, arg] (const juce::Array<var>& a, Completion c) { c (eng->chooseSectionOption ((int) arg (a, 0), (int) arg (a, 1))); })
        .withNativeFunction ("setAiKey", [eng, arg] (const juce::Array<var>& a, Completion c)
        {
            AiMoodClient::setKey (arg (a, 0).toString());
            c (eng->aiStatus());
        })
        .withNativeFunction ("getAiStatus", [eng] (const juce::Array<var>&, Completion c) { c (eng->aiStatus()); });

    return options
        .withNativeFunction ("parseChord", [] (const juce::Array<var>& args, Completion complete)
        {
            complete (parseChord (args.isEmpty() ? juce::String() : json::repairUtf8 (args[0].toString())));
        })
        .withNativeFunction ("setProgression", [proc] (const juce::Array<var>& args, Completion complete)
        {
            complete (args.isEmpty() ? var (false) : setProgression (*proc, json::repair (args[0])));
        })
        .withNativeFunction ("getSession", [proc] (const juce::Array<var>&, Completion complete)
        {
            auto* o = new juce::DynamicObject();
            for (int i = 0; i < proc->session.getNumProperties(); ++i)
            {
                const auto name = proc->session.getPropertyName (i);
                o->setProperty (name, proc->session.getProperty (name));
            }
            complete (var (o));
        })
        .withNativeFunction ("setSessionValue", [proc] (const juce::Array<var>& args, Completion complete)
        {
            // Only simple, namespaced UI keys are accepted so the UI can't clobber engine state.
            if (args.size() == 2 && args[0].toString().startsWith ("ui."))
            {
                proc->session.setProperty (juce::Identifier (args[0].toString().replaceCharacter ('.', '_')), json::repair (args[1]), nullptr);
                complete (true);
                return;
            }
            complete (false);
        })
        .withNativeFunction ("exportMidi", [exportMidi] (const juce::Array<var>&, Completion complete)
        {
            if (exportMidi)
                exportMidi();
            complete (true);
        });
}

} // namespace so::bridge
