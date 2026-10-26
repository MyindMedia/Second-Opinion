#include "ChordSymbolParser.h"

#include <algorithm>
#include <array>

namespace so::theory
{

namespace
{
    struct Alias { std::string_view text; ChordQuality quality; };

    // Every spelling we accept for each quality. Matching is longest-first, so "maj7" wins over "maj"
    // and "m7b5" wins over "m7". Includes everything ChordNamer can emit (Pop, Jazz, ASCII, Unicode).
    const std::vector<Alias>& aliases()
    {
        using Q = ChordQuality;

        static const std::vector<Alias> list = []
        {
            std::vector<Alias> a {
                { "", Q::Major }, { "maj", Q::Major }, { "M", Q::Major }, { "major", Q::Major },
                { "m", Q::Minor }, { "min", Q::Minor }, { "mi", Q::Minor }, { "-", Q::Minor }, { "minor", Q::Minor },
                { "dim", Q::Diminished }, { "°", Q::Diminished }, { "o", Q::Diminished },
                { "aug", Q::Augmented }, { "+", Q::Augmented }, { "#5", Q::Augmented }, { "♯5", Q::Augmented },
                { "sus2", Q::Sus2 }, { "sus4", Q::Sus4 }, { "sus", Q::Sus4 },
                { "6", Q::Six }, { "maj6", Q::Six }, { "M6", Q::Six },
                { "m6", Q::MinorSix }, { "min6", Q::MinorSix }, { "-6", Q::MinorSix },
                { "7", Q::Dominant7 }, { "dom7", Q::Dominant7 },
                { "maj7", Q::Major7 }, { "M7", Q::Major7 }, { "Δ7", Q::Major7 }, { "Δ", Q::Major7 },
                { "ma7", Q::Major7 }, { "^7", Q::Major7 }, { "^", Q::Major7 },
                { "m7", Q::Minor7 }, { "min7", Q::Minor7 }, { "mi7", Q::Minor7 }, { "-7", Q::Minor7 },
                { "m7b5", Q::HalfDiminished7 }, { "m7♭5", Q::HalfDiminished7 }, { "ø7", Q::HalfDiminished7 },
                { "ø", Q::HalfDiminished7 }, { "-7b5", Q::HalfDiminished7 }, { "-7♭5", Q::HalfDiminished7 },
                { "min7b5", Q::HalfDiminished7 },
                { "dim7", Q::Diminished7 }, { "°7", Q::Diminished7 }, { "o7", Q::Diminished7 },
                { "add9", Q::Add9 }, { "add2", Q::Add9 }, { "(add9)", Q::Add9 },
                { "9", Q::Dominant9 },
                { "maj9", Q::Major9 }, { "M9", Q::Major9 }, { "Δ9", Q::Major9 }, { "ma9", Q::Major9 },
                { "m9", Q::Minor9 }, { "min9", Q::Minor9 }, { "-9", Q::Minor9 },
                { "7sus4", Q::Dominant7Sus4 }, { "7sus", Q::Dominant7Sus4 }, { "9sus4", Q::Dominant7Sus4 },
            };

            std::stable_sort (a.begin(), a.end(), [] (const Alias& x, const Alias& y) { return x.text.size() > y.text.size(); });
            return a;
        }();

        return list;
    }

    std::string_view trim (std::string_view s)
    {
        while (! s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix (1);
        while (! s.empty() && (s.back() == ' ' || s.back() == '\t'))   s.remove_suffix (1);
        return s;
    }
}

ParseResult ChordSymbolParser::parse (std::string_view input)
{
    auto text = trim (input);

    if (text.empty())
        return ParseError { "Type a chord, for example G/B or Am7", 0 };

    auto root = parsePitchPrefix (text);
    if (! root)
        return ParseError { "Start with a note name (A to G)", 0 };

    Chord chord;
    chord.root = root->first;

    // Split off the bass at the last '/', so "C6/9"-style input is not misread (not in vocab, rejected below).
    auto rest = text.substr (root->second);
    std::string_view qualityText = rest;
    std::string_view bassText;

    if (const auto slash = rest.rfind ('/'); slash != std::string_view::npos)
    {
        qualityText = trim (rest.substr (0, slash));
        bassText = trim (rest.substr (slash + 1));

        if (bassText.empty())
            return ParseError { "Add a bass note after the slash, for example G/B", text.size() };
    }

    bool matched = false;
    for (const auto& alias : aliases())
    {
        if (qualityText == alias.text)
        {
            chord.quality = alias.quality;
            matched = true;
            break;
        }
    }

    if (! matched)
        return ParseError { "Unknown chord type \"" + std::string (qualityText) + "\". Try m, 7, maj7, m7, m7b5, dim7, sus4, add9 or 9",
                            root->second };

    if (! bassText.empty())
    {
        auto bass = parsePitch (bassText);
        if (! bass)
            return ParseError { "The bass after the slash must be a note name, for example /B or /F#",
                                text.size() - bassText.size() };

        // "C/C" is just C.
        if (bass->pc() != chord.root.pc())
            chord.bass = *bass;
    }

    return chord;
}

std::optional<Chord> ChordSymbolParser::tryParse (std::string_view symbol)
{
    auto r = parse (symbol);
    if (auto* c = std::get_if<Chord> (&r))
        return *c;
    return std::nullopt;
}

} // namespace so::theory
