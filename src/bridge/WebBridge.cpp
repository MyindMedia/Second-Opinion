#include "WebBridge.h"

#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"
#include "plugin/PluginProcessor.h"

namespace so::bridge
{

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

    return options
        .withNativeFunction ("parseChord", [] (const juce::Array<var>& args, Completion complete)
        {
            complete (parseChord (args.isEmpty() ? juce::String() : args[0].toString()));
        })
        .withNativeFunction ("setProgression", [proc] (const juce::Array<var>& args, Completion complete)
        {
            complete (args.isEmpty() ? var (false) : setProgression (*proc, args[0]));
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
                proc->session.setProperty (juce::Identifier (args[0].toString().replaceCharacter ('.', '_')), args[1], nullptr);
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
