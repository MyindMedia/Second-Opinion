#include "SongBuilder.h"

#include "../theory/ChordNamer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <optional>
#include <set>

namespace so::song
{

using theory::Chord;
using theory::ChordQuality;
using theory::HarmonicFunction;
using theory::Key;
using theory::Mode;
using theory::QualityFamily;
using theory::RomanNumeral;
using theory::SpelledPitch;
using theory::wrapPc;

namespace
{
    // =============================================================================================
    // Small utilities
    // =============================================================================================

    struct Rng
    {
        std::uint64_t state;

        std::uint64_t next() noexcept
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        float uniform() noexcept { return (float) ((double) (next() >> 11) * (1.0 / 9007199254740992.0)); }
    };

    std::uint64_t hashString (std::string_view s) noexcept
    {
        std::uint64_t h = 1469598103934665603ull;
        for (char c : s) { h ^= (unsigned char) c; h *= 1099511628211ull; }
        return h;
    }

    float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

    std::string pop (const Chord& c)
    {
        return theory::ChordNamer::name (c, {});
    }

    std::string lowerFirst (std::string s)
    {
        for (const char* proper : { "Neapolitan", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Andalusian", "Picardy", "Aeolian" })
            if (s.rfind (proper, 0) == 0)
                return s;
        if (! s.empty() && s[0] >= 'A' && s[0] <= 'Z' && ! (s.size() > 1 && s[1] >= 'A' && s[1] <= 'Z'))
            s[0] = (char) (s[0] - 'A' + 'a');
        return s;
    }

    std::string styleName (Style s)
    {
        switch (s)
        {
            case Style::RnB: return "R&B";
            case Style::NeoSoul: return "neo-soul";
            case Style::HipHop: return "hip-hop";
            case Style::Trap: return "trap";
            case Style::Drill: return "drill";
            case Style::Pop: return "pop";
            case Style::Gospel: return "gospel";
            case Style::LoFi: return "lo-fi";
            case Style::Cinematic: return "cinematic";
            case Style::Rock: return "rock";
            case Style::EDM: return "EDM";
            case Style::Afrobeats: return "Afrobeats";
            case Style::Latin: return "Latin";
            case Style::Jazz: return "jazz";
            case Style::Country: return "country";
            case Style::Auto: case Style::Count: break;
        }
        return "pop";
    }

    bool isPopFamily (Style s)
    {
        return s == Style::Pop || s == Style::Rock || s == Style::Country || s == Style::EDM || s == Style::Cinematic;
    }

    bool isDarkUrban (Style s) { return s == Style::Trap || s == Style::Drill || s == Style::HipHop; }

    float styleSophistication (Style s)
    {
        switch (s)
        {
            case Style::RnB: return 0.7f;     case Style::NeoSoul: return 0.95f; case Style::HipHop: return 0.4f;
            case Style::Trap: return 0.15f;   case Style::Drill: return 0.05f;   case Style::Pop: return 0.3f;
            case Style::Gospel: return 0.8f;  case Style::LoFi: return 0.85f;    case Style::Cinematic: return 0.25f;
            case Style::Rock: return 0.05f;   case Style::EDM: return 0.3f;      case Style::Afrobeats: return 0.55f;
            case Style::Latin: return 0.5f;   case Style::Jazz: return 1.0f;     case Style::Country: return 0.05f;
            case Style::Auto: case Style::Count: break;
        }
        return 0.4f;
    }

    double styleChordsPerBar (Style s)
    {
        switch (s)
        {
            case Style::Trap: case Style::Drill: case Style::Cinematic: return 0.5;
            case Style::Gospel: case Style::Jazz: return 2.0;
            default: return 1.0;
        }
    }

    double styleBpm (Style s)
    {
        switch (s)
        {
            case Style::Trap: return 140; case Style::Drill: return 142; case Style::HipHop: return 90;
            case Style::RnB: return 90;   case Style::NeoSoul: return 84; case Style::LoFi: return 80;
            case Style::Gospel: return 76; case Style::EDM: return 126; case Style::Afrobeats: return 104;
            case Style::Latin: return 96; case Style::Cinematic: return 80; case Style::Jazz: return 120;
            case Style::Country: return 100; case Style::Rock: return 120; default: return 110;
        }
    }

    Style resolveStyle (const MoodProfile& m)
    {
        if (m.style != Style::Auto) return m.style;
        if (m.sophistication > 0.65f && m.energy < 0.55f) return m.sophistication > 0.85f ? Style::NeoSoul : Style::RnB;
        if (m.darkness > 0.65f && m.energy > 0.55f) return m.darkness > 0.85f ? Style::Drill : Style::Trap;
        if (m.energy > 0.7f && m.valence > 0.2f) return m.energy > 0.85f ? Style::EDM : Style::Pop;
        if (m.energy < 0.35f && m.sophistication > 0.45f) return Style::LoFi;
        if (m.darkness > 0.55f) return m.tension > 0.45f ? Style::Cinematic : Style::HipHop;
        return Style::Pop;
    }

    SpelledPitch tidy (SpelledPitch p, bool preferFlats)
    {
        if (p.accidental <= -2 || p.accidental >= 2)
            return SpelledPitch::fromPc (p.pc(), preferFlats);
        return p;
    }

    /** Pitch `semis` above the key's tonic, spelled on scale degree `steps` (0..6). */
    SpelledPitch degreePitch (const Key& key, int semis, int steps)
    {
        return tidy (SpelledPitch::above (key.tonic, wrapPc (semis), ((steps % 7) + 7) % 7), key.signature() <= 0);
    }

    int letterSteps (const SpelledPitch& from, const SpelledPitch& to)
    {
        return (((int) to.letter - (int) from.letter) % 7 + 7) % 7;
    }

    int relOf (const Chord& c, const Key& k) { return wrapPc (c.root.pc() - k.tonic.pc()); }

    Key shiftedKey (const Key& key, int semis)
    {
        const int pc = wrapPc (key.tonic.pc() + semis);
        const Key flat { SpelledPitch::fromPc (pc, true), key.mode };
        const Key sharp { SpelledPitch::fromPc (pc, false), key.mode };
        return std::abs (sharp.signature()) < std::abs (flat.signature()) ? sharp : flat;
    }

    Chord transposeChord (const Chord& c, const Key& from, const Key& to)
    {
        if (from == to) return c;
        Chord out = c;
        out.root = degreePitch (to, wrapPc (c.root.pc() - from.tonic.pc()), letterSteps (from.tonic, c.root));
        if (c.bass)
            out.bass = degreePitch (to, wrapPc (c.bass->pc() - from.tonic.pc()), letterSteps (from.tonic, *c.bass));
        return out;
    }

    /** Chord tone on `degree` (3, 5, 7), spelled from the root; nullopt when the quality has none. */
    std::optional<SpelledPitch> chordToneOn (const Chord& c, int degree)
    {
        for (const auto& iv : theory::info (c.quality).intervals)
            if (iv.degree == degree)
                return tidy (SpelledPitch::above (c.root, iv.semitones, iv.letterSteps()), true);
        return std::nullopt;
    }

    Chord makeChord (const SpelledPitch& root, ChordQuality q, std::optional<SpelledPitch> bass = std::nullopt)
    {
        Chord c;
        c.root = root;
        c.quality = q;
        if (bass && bass->pc() != root.pc())
            c.bass = bass;
        return c;
    }

    Chord tonicChord (const Key& key, ChordQuality q) { return makeChord (key.tonic, q); }

    QualityFamily familyOf (ChordQuality q) { return theory::info (q).family; }

    bool isMajorish (ChordQuality q)
    {
        const auto f = familyOf (q);
        return f == QualityFamily::Major || f == QualityFamily::Dominant;
    }

    bool isMinorish (ChordQuality q) { return familyOf (q) == QualityFamily::Minor; }

    // =============================================================================================
    // Template library
    // =============================================================================================

    enum class R { Verse, Chorus, Pre, Bridge };

#define ST(x) (1u << (int) Style::x)
    constexpr unsigned URBAN = ST (HipHop) | ST (Trap) | ST (Drill);
    constexpr unsigned SOUL = ST (RnB) | ST (NeoSoul) | ST (Gospel) | ST (Jazz) | ST (LoFi);

    struct Template
    {
        const char* id;
        R role;
        bool minor;
        const char* chords;
        unsigned styles;
        float bright, tension, energy;
        const char* title;
        const char* why;
        int keyShift = 0;
    };

    const std::vector<Template>& templates()
    {
        static const std::vector<Template> table {
#include "HarmonyLibrary.inc"
        };
        return table;
    }
#undef ST

    struct Slot
    {
        Chord chord;
        double beats;
    };

    using Pattern = std::vector<Slot>;

    std::optional<std::pair<int, int>> parseRoman (std::string_view& s, bool& upper)
    {
        int acc = 0;
        while (! s.empty() && (s.front() == 'b' || s.front() == '#'))
        {
            acc += s.front() == 'b' ? -1 : 1;
            s.remove_prefix (1);
        }
        size_t n = 0;
        while (n < s.size() && (s[n] == 'I' || s[n] == 'V' || s[n] == 'i' || s[n] == 'v'))
            ++n;
        if (n == 0) return std::nullopt;
        std::string r;
        for (size_t i = 0; i < n; ++i) r += (char) ((s[i] == 'i') ? 'I' : (s[i] == 'v' ? 'V' : s[i]));
        upper = s[0] == 'I' || s[0] == 'V';
        s.remove_prefix (n);
        static const std::array<std::string_view, 7> names { "I", "II", "III", "IV", "V", "VI", "VII" };
        static constexpr std::array<int, 7> semis { 0, 2, 4, 5, 7, 9, 11 };
        for (int d = 0; d < 7; ++d)
            if (r == names[(size_t) d])
                return std::make_pair (semis[(size_t) d] + acc, d);
        return std::nullopt;
    }

    std::optional<ChordQuality> parseSuffix (std::string_view s, bool upper)
    {
        using Q = ChordQuality;
        static const std::array<std::pair<std::string_view, Q>, 11> up {{
            { "", Q::Major }, { "7", Q::Dominant7 }, { "maj7", Q::Major7 }, { "maj9", Q::Major9 }, { "9", Q::Dominant9 },
            { "6", Q::Six }, { "add9", Q::Add9 }, { "sus2", Q::Sus2 }, { "sus4", Q::Sus4 }, { "7sus4", Q::Dominant7Sus4 },
            { "+", Q::Augmented } }};
        static const std::array<std::pair<std::string_view, Q>, 7> lo {{
            { "", Q::Minor }, { "7", Q::Minor7 }, { "9", Q::Minor9 }, { "6", Q::MinorSix }, { "o", Q::Diminished },
            { "o7", Q::Diminished7 }, { "h7", Q::HalfDiminished7 } }};
        if (upper) { for (const auto& [k, q] : up) if (k == s) return q; }
        else       { for (const auto& [k, q] : lo) if (k == s) return q; }
        return std::nullopt;
    }

    /** Parses one template token ("bVImaj7/3:0.5") into a chord in `key`. */
    std::optional<Slot> parseToken (std::string_view tok, const Key& key, double slotBeats)
    {
        double mult = 1.0;
        if (const auto colon = tok.find (':'); colon != std::string_view::npos)
        {
            mult = std::atof (std::string (tok.substr (colon + 1)).c_str());
            tok = tok.substr (0, colon);
            if (mult <= 0.0) return std::nullopt;
        }
        std::string_view bassPart;
        if (const auto slash = tok.find ('/'); slash != std::string_view::npos)
        {
            bassPart = tok.substr (slash + 1);
            tok = tok.substr (0, slash);
        }
        bool upper = true;
        auto rn = parseRoman (tok, upper);
        if (! rn) return std::nullopt;
        auto q = parseSuffix (tok, upper);
        if (! q) return std::nullopt;

        Chord c = makeChord (degreePitch (key, rn->first, rn->second), *q);
        if (! bassPart.empty())
        {
            if (bassPart == "3" || bassPart == "5" || bassPart == "7")
            {
                if (auto b = chordToneOn (c, bassPart[0] - '0'))
                    c = makeChord (c.root, c.quality, *b);
            }
            else
            {
                bool bu = true;
                auto bv = bassPart;
                if (auto br = parseRoman (bv, bu))
                    c = makeChord (c.root, c.quality, degreePitch (key, br->first, br->second));
            }
        }
        return Slot { c, slotBeats * mult };
    }

    Pattern parsePattern (std::string_view text, const Key& key, double slotBeats)
    {
        Pattern p;
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && text[i] == ' ') ++i;
            size_t j = i;
            while (j < text.size() && text[j] != ' ') ++j;
            if (j > i)
                if (auto s = parseToken (text.substr (i, j - i), key, slotBeats))
                    p.push_back (*s);
            i = j;
        }
        return p;
    }

    double patternBeats (const Pattern& p)
    {
        double b = 0;
        for (const auto& s : p) b += s.beats;
        return b;
    }

    // =============================================================================================
    // Color (scale-aware extensions)
    // =============================================================================================

    bool fitsScale (const Chord& c, const Key& k)
    {
        const int lt = wrapPc (k.tonic.pc() + 11);
        const int rel = relOf (c, k);
        const bool allowLt = k.mode == Mode::Minor && (rel == 7 || rel == 11);
        for (int pc : c.chordTonePcs())
            if (! k.inScale (pc) && ! (allowLt && pc == lt))
                return false;
        return true;
    }

    bool inScaleOrLt (const Key& k, int pc, bool allowLt)
    {
        return k.inScale (pc) || (allowLt && wrapPc (pc - k.tonic.pc()) == 11);
    }

    enum class ColorChange { None, Up, Down };

    /** Adds or removes 7ths and 9ths so the chord matches the color level, using only notes that
        belong to the key (or its parallel key for borrowed chords). Suspended, diminished and
        augmented chords are left alone. */
    Chord colorize (const Chord& in, const Key& key, float level, Style style, ColorChange* change = nullptr)
    {
        using Q = ChordQuality;
        Chord c = in;
        const Q q = in.quality;
        const bool majorish = q == Q::Major || q == Q::Major7 || q == Q::Major9 || q == Q::Add9 || q == Q::Six
                           || q == Q::Dominant7 || q == Q::Dominant9;
        const bool minorish = q == Q::Minor || q == Q::Minor7 || q == Q::Minor9 || q == Q::MinorSix;
        if (! majorish && ! minorish)
            return c;

        Chord triad = makeChord (in.root, majorish ? Q::Major : Q::Minor);
        const Key* sk = nullptr;
        const Key par = key.parallel();
        if (fitsScale (triad, key)) sk = &key;
        else if (fitsScale (triad, par)) sk = &par;

        const int rpc = in.root.pc();
        const int rel = relOf (in, key);
        const bool allowLt = sk != nullptr && sk->mode == Mode::Minor && (rel == 7 || rel == 11);
        const bool has11 = sk && inScaleOrLt (*sk, rpc + 11, allowLt);
        const bool has10 = sk && inScaleOrLt (*sk, rpc + 10, allowLt);
        const bool has9 = sk && inScaleOrLt (*sk, rpc + 2, allowLt);
        const bool dominantRole = q == Q::Dominant7 || q == Q::Dominant9 || (majorish && rel == 7);
        const bool popFam = isPopFamily (style);

        Q target = q;
        if (level < 0.28f)
        {
            if (q == Q::Major7 || q == Q::Major9 || q == Q::Add9 || q == Q::Six) target = Q::Major;
            else if (q == Q::Minor7 || q == Q::Minor9 || q == Q::MinorSix) target = Q::Minor;
            else if (q == Q::Dominant9) target = Q::Dominant7;
        }
        else if (sk == nullptr)
        {
            // Chromatic chord: only make sure a dominant-shaped major triad on V gets its seventh.
            if (q == Q::Major9) target = Q::Major7;
            if (q == Q::Minor9) target = Q::Minor7;
        }
        else if (level < 0.5f)
        {
            if (q == Q::Major9) target = Q::Major7;
            else if (q == Q::Minor9) target = Q::Minor7;
            else if (q == Q::Dominant9) target = Q::Dominant7;
            else if (q == Q::Major && popFam && level >= 0.4f && ! dominantRole && has9 && (rel == 0 || rel == 5 || rel == 8 || rel == 3))
                target = Q::Add9;
            else if (q == Q::Minor && ! popFam && level >= 0.4f && has10)
                target = Q::Minor7;
        }
        else if (level < 0.72f)
        {
            if (q == Q::Major)
                target = dominantRole ? (has10 ? Q::Dominant7 : q) : (has11 ? (popFam ? (has9 ? Q::Add9 : Q::Major7) : Q::Major7) : q);
            else if (q == Q::Minor && has10)
                target = Q::Minor7;
            else if (q == Q::Dominant9 && popFam)
                target = Q::Dominant7;
        }
        else
        {
            if (q == Q::Major || q == Q::Major7 || q == Q::Add9)
            {
                if (dominantRole) target = has10 ? ((has9 && ! popFam) ? Q::Dominant9 : Q::Dominant7) : q;
                else if (has11) target = (has9 && ! popFam) ? Q::Major9 : Q::Major7;
                else if (has10 && ! popFam && q == Q::Major) target = has9 ? Q::Dominant9 : Q::Dominant7;
            }
            else if ((q == Q::Minor || q == Q::Minor7) && has10)
                target = (has9 && ! popFam) ? Q::Minor9 : Q::Minor7;
            else if (q == Q::Dominant7 && has9 && ! popFam && dominantRole)
                target = Q::Dominant9;
        }

        if (target != q)
        {
            const int before = (int) theory::info (q).intervals.size();
            const int after = (int) theory::info (target).intervals.size();
            if (change && *change == ColorChange::None)
                *change = after > before ? ColorChange::Up : (after < before ? ColorChange::Down : ColorChange::None);
            c.quality = target;
            // A chord-tone bass that no longer exists (a 7th in the bass of a triad) stays as written: slash chord.
        }
        return c;
    }

    // =============================================================================================
    // Measures
    // =============================================================================================

    float chordBrightness (const Chord& c, const Key& key)
    {
        float b = 0.0f;
        switch (familyOf (c.quality))
        {
            case QualityFamily::Major: b = 0.6f; break;
            case QualityFamily::Dominant: b = 0.3f; break;
            case QualityFamily::Minor: b = -0.5f; break;
            case QualityFamily::Diminished: b = -0.8f; break;
            case QualityFamily::Augmented: b = 0.0f; break;
            case QualityFamily::Suspended: b = 0.1f; break;
        }
        const int rel = relOf (c, key);
        if (rel == 1) b -= 0.4f;
        if (key.mode == Mode::Major && (rel == 8 || rel == 10 || rel == 3)) b -= 0.2f;
        if (key.mode == Mode::Minor) b -= 0.15f;
        return b;
    }

    float chordTension (const Chord& c, const Key& key)
    {
        const auto rn = RomanNumeral::analyze (c, key);
        float t = 0.5f;
        switch (rn.function)
        {
            case HarmonicFunction::Tonic: t = 0.1f; break;
            case HarmonicFunction::Predominant: t = 0.35f; break;
            case HarmonicFunction::Dominant: t = 0.65f; break;
            case HarmonicFunction::Other: t = 0.5f; break;
        }
        t += 0.08f * (float) std::max (0, (int) theory::info (c.quality).intervals.size() - 3);
        const auto fam = familyOf (c.quality);
        if (fam == QualityFamily::Suspended) t += 0.15f;
        if (fam == QualityFamily::Diminished) t += 0.2f;
        if (! rn.diatonic) t += 0.1f;
        return clamp01 (t);
    }

    template <typename Seq, typename F>
    float weightedMean (const Seq& seq, F f)
    {
        double sum = 0, w = 0;
        for (const auto& s : seq) { sum += f (s) * s.beats; w += s.beats; }
        return w > 0 ? (float) (sum / w) : 0.0f;
    }

    float roleEnergy (Section s)
    {
        switch (s)
        {
            case Section::Intro: return 0.3f;      case Section::Verse: return 0.45f;
            case Section::PreChorus: return 0.6f;  case Section::Chorus: return 0.8f;
            case Section::PostChorus: return 0.72f; case Section::Bridge: return 0.55f;
            case Section::Breakdown: return 0.3f;  case Section::Outro: return 0.25f;
            case Section::Count: break;
        }
        return 0.5f;
    }

    // =============================================================================================
    // Layout
    // =============================================================================================

    std::vector<SongChord> tile (const Pattern& p, double totalBeats)
    {
        std::vector<SongChord> out;
        const double loop = patternBeats (p);
        if (p.empty() || loop <= 0.0) return out;
        double t = 0.0;
        while (t < totalBeats - 1e-9)
        {
            for (const auto& s : p)
            {
                if (t >= totalBeats - 1e-9) break;
                const double len = std::min (s.beats, totalBeats - t);
                out.push_back ({ t, len, s.chord, {}, HarmonicFunction::Other });
                t += s.beats;
            }
        }
        return out;
    }

    void mergeRepeats (std::vector<SongChord>& v)
    {
        std::vector<SongChord> out;
        for (const auto& c : v)
        {
            if (! out.empty() && out.back().chord == c.chord
                && std::fabs (out.back().startBeat + out.back().beats - c.startBeat) < 1e-9)
                out.back().beats += c.beats;
            else
                out.push_back (c);
        }
        v = std::move (out);
    }

    /** Lays a template pattern over the section, halving the harmonic rhythm until the loop fits. */
    std::vector<SongChord> layoutPattern (Pattern p, double totalBeats)
    {
        double loop = patternBeats (p);
        while (loop > totalBeats + 1e-9 && loop > 0)
        {
            bool canHalve = true;
            for (const auto& s : p) canHalve = canHalve && s.beats >= 2.0;
            if (! canHalve) break;
            for (auto& s : p) s.beats *= 0.5;
            loop *= 0.5;
        }
        auto out = tile (p, totalBeats);
        mergeRepeats (out);
        return out;
    }

    Pattern toPattern (const std::vector<SongChord>& chords, double loopBeats)
    {
        Pattern p;
        for (size_t i = 0; i < chords.size(); ++i)
        {
            const double end = i + 1 < chords.size() ? chords[i + 1].startBeat : loopBeats;
            p.push_back ({ chords[i].chord, std::max (0.25, end - chords[i].startBeat) });
        }
        return p;
    }

    std::vector<std::string> symbolsOf (const std::vector<SongChord>& v)
    {
        std::vector<std::string> s;
        for (const auto& c : v) s.push_back (pop (c.chord) + "@" + std::to_string ((int) std::lround (c.beats * 4)));
        return s;
    }

    std::vector<std::string> plainSymbols (const std::vector<SongChord>& v)
    {
        std::vector<std::string> s;
        for (const auto& c : v) s.push_back (pop (c.chord));
        return s;
    }

    // =============================================================================================
    // Transitions
    // =============================================================================================

    bool sharesTone (const Chord& a, const Chord& b)
    {
        const auto pa = a.allPcs();
        const auto pb = b.allPcs();
        for (int x : pa)
            if (std::find (pb.begin(), pb.end(), x) != pb.end())
                return true;
        return false;
    }

    std::string transitionPhrase (Transition t)
    {
        switch (t)
        {
            case Transition::Dominant: return "dominant of the next chord";
            case Transition::HalfStepAbove: return "half step approach";
            case Transition::Backdoor: return "backdoor flat seven";
            case Transition::Plagal: return "plagal motion";
            case Transition::Stepwise: return "stepwise bass";
            case Transition::CommonTone: return "common-tone pivot";
            case Transition::None: break;
        }
        return "direct cut";
    }

    struct Ctx
    {
        Key key;
        Style style;
        float level;      // color level 0..1
        MoodProfile mood; // section mood after role adjustments
    };

    ChordQuality dominantQuality (const Ctx& cx, bool targetMinor)
    {
        if (cx.level < 0.4f) return ChordQuality::Major;
        if (cx.level >= 0.72f && ! targetMinor && ! isPopFamily (cx.style)) return ChordQuality::Dominant9;
        return ChordQuality::Dominant7;
    }

    /** Approach chords into `target` for the style (one or two chords). */
    std::vector<Chord> approachFor (const Chord& target, const Ctx& cx, bool modulating)
    {
        const bool tMinor = isMinorish (target.quality) || familyOf (target.quality) == QualityFamily::Diminished;
        const bool flats = cx.key.signature() <= 0;
        auto at = [&] (int semis, int steps) { return tidy (SpelledPitch::above (target.root, semis, steps), flats); };
        const auto dom = makeChord (at (7, 4), dominantQuality (cx, tMinor));
        const float level = cx.level;

        if (modulating)
            return { makeChord (at (7, 4), ChordQuality::Dominant7) };

        switch (cx.style)
        {
            case Style::Gospel:
                return { makeChord (at (7, 4), ChordQuality::Dominant7Sus4), makeChord (at (7, 4), ChordQuality::Dominant7) };
            case Style::NeoSoul: case Style::Jazz: case Style::LoFi:
                if (cx.mood.tension > 0.55f)
                    return { makeChord (at (1, 1), ChordQuality::Dominant7) };
                return { makeChord (at (2, 1), tMinor ? ChordQuality::HalfDiminished7 : ChordQuality::Minor7),
                         makeChord (at (7, 4), ChordQuality::Dominant7) };
            case Style::RnB:
                if (level >= 0.6f)
                    return { makeChord (at (2, 1), tMinor ? ChordQuality::HalfDiminished7 : ChordQuality::Minor7),
                             makeChord (at (7, 4), ChordQuality::Dominant7) };
                return { makeChord (at (7, 4), ChordQuality::Dominant7Sus4), makeChord (at (7, 4), ChordQuality::Dominant7) };
            case Style::EDM:
                return { makeChord (at (7, 4), ChordQuality::Sus4), makeChord (at (7, 4), ChordQuality::Major) };
            case Style::Cinematic:
                return { makeChord (at (10, 6), ChordQuality::Major) };
            case Style::Trap: case Style::Drill: case Style::HipHop:
                if (tMinor && cx.mood.darkness > 0.6f)
                    return { makeChord (at (1, 1), level >= 0.5f ? ChordQuality::Major7 : ChordQuality::Major) };
                return { makeChord (at (10, 6), ChordQuality::Major) };
            default:
                break;
        }
        return { dom };
    }

    /** Returns the body with its ending turned toward `target`, and the sentence describing it. */
    std::vector<SongChord> applyTransition (const std::vector<SongChord>& body, const Chord& target,
                                            const Ctx& cx, bool modulating, std::string& sentence,
                                            const std::string& nextLabel)
    {
        auto out = body;
        if (out.empty()) return out;

        const auto& last = out.back().chord;
        const auto cur = transitionBetween (last, target);
        const auto fn = RomanNumeral::analyze (last, cx.key).function;
        const bool ok = modulating ? cur == Transition::Dominant
                                   : (isStrongTransition (cur)
                                      || (cur == Transition::CommonTone && (fn == HarmonicFunction::Dominant || fn == HarmonicFunction::Predominant)));
        if (ok)
        {
            const int lastRel = relOf (last, cx.key);
            const int targetRel = relOf (target, cx.key);
            const bool deceptive = ! modulating && fn == HarmonicFunction::Dominant && lastRel == 7
                                && (targetRel == 8 || targetRel == 9);
            sentence = "It ends on " + pop (last) + ", which leads into " + pop (target) + " at the top of the "
                     + nextLabel + " (" + (deceptive ? std::string ("deceptive cadence") : transitionPhrase (cur)) + ").";
            return out;
        }

        const auto approach = approachFor (target, cx, modulating);
        const double lastBeats = out.back().beats;
        const double span = lastBeats >= 8.0 ? 4.0 : (lastBeats >= 4.0 ? lastBeats / 2.0 : lastBeats);
        SongChord tail = out.back();
        out.pop_back();
        if (lastBeats - span > 1e-9)
        {
            SongChord head = tail;
            head.beats = lastBeats - span;
            out.push_back (head);
        }
        double t = tail.startBeat + (lastBeats - span);
        const double each = span / (double) approach.size();
        for (const auto& a : approach)
        {
            out.push_back ({ t, each, a, {}, HarmonicFunction::Other });
            t += each;
        }
        mergeRepeats (out);

        const auto kind = transitionBetween (approach.back(), target);
        std::string names;
        for (size_t i = 0; i < approach.size(); ++i)
            names += (i ? " to " : "") + pop (approach[i]);
        if (modulating)
            sentence = "The last bar turns to " + names + ", the five of the next chord in the new key, so the " + nextLabel + " lands higher (modulation).";
        else
            sentence = "The last bar turns to " + names + " to pull into the " + nextLabel + " (" + transitionPhrase (kind) + ").";
        return out;
    }

    // =============================================================================================
    // Candidates
    // =============================================================================================

    struct Candidate
    {
        std::string id, title, why, colorNote;
        std::vector<SongChord> body;
        float bright { 0 }, tension { 0.3f }, energy { 0.5f };
        float score { 0 };
        bool derived { false };
        unsigned styles { 0 };
        int keyShift { 0 };
    };

    std::string colorNoteFor (ColorChange ch, Style style)
    {
        if (ch == ColorChange::Up)
            return isPopFamily (style) ? "Added colors keep it fresh without getting jazzy."
                                       : "Sevenths and ninths on top give it the warm " + styleName (style) + " sound.";
        if (ch == ColorChange::Down)
            return "Kept to plain triads so it stays punchy and leaves room for the drums.";
        return {};
    }

    Candidate fromTemplate (const Template& t, const Ctx& cx, double slotBeats, double totalBeats)
    {
        Candidate c;
        c.id = t.id;
        c.title = t.title;
        c.why = t.why;
        c.styles = t.styles;
        c.bright = t.bright;
        c.tension = t.tension;
        c.energy = t.energy;
        c.keyShift = t.keyShift;
        auto pat = parsePattern (t.chords, cx.key, slotBeats);
        ColorChange ch = ColorChange::None;
        for (auto& s : pat)
            s.chord = colorize (s.chord, cx.key, cx.level, cx.style, &ch);
        c.colorNote = colorNoteFor (ch, cx.style);
        c.body = layoutPattern (pat, totalBeats);
        return c;
    }

    Candidate derivedCandidate (std::string id, std::string title, std::string why, const Pattern& p,
                                const Ctx& cx, double totalBeats, bool compress)
    {
        Candidate c;
        c.id = std::move (id);
        c.title = std::move (title);
        c.why = std::move (why);
        c.derived = true;
        c.body = compress ? layoutPattern (p, totalBeats) : tile (p, totalBeats);
        if (! compress) mergeRepeats (c.body);
        c.bright = weightedMean (c.body, [&] (const SongChord& s) { return chordBrightness (s.chord, cx.key); });
        c.tension = weightedMean (c.body, [&] (const SongChord& s) { return chordTension (s.chord, cx.key); });
        c.energy = 0.5f;
        return c;
    }

    Pattern pedalOf (const Pattern& p, const Key& key)
    {
        Pattern out = p;
        for (auto& s : out)
            s.chord = makeChord (s.chord.root, s.chord.quality, key.tonic);
        return out;
    }

    Pattern colorUp (const Pattern& p, const Ctx& cx)
    {
        Pattern out = p;
        for (auto& s : out)
            s.chord = colorize (s.chord, cx.key, std::max (0.72f, cx.level + 0.3f), cx.style == Style::Auto ? Style::RnB : cx.style);
        return out;
    }

    Pattern relativeSwap (const Pattern& p, const Key& key, float level)
    {
        Pattern out = p;
        for (auto& s : out)
        {
            if (relOf (s.chord, key) != 0) continue;
            if (key.mode == Mode::Minor && isMinorish (s.chord.quality))
                s.chord = makeChord (degreePitch (key, 3, 2), level >= 0.5f ? ChordQuality::Major7 : ChordQuality::Major);
            else if (key.mode == Mode::Major && isMajorish (s.chord.quality))
                s.chord = makeChord (degreePitch (key, 9, 5), level >= 0.5f ? ChordQuality::Minor7 : ChordQuality::Minor);
        }
        return out;
    }

    Pattern walkingBass (const Pattern& p)
    {
        Pattern out = p;
        if (out.size() < 2) return out;
        int prevBass = out[0].chord.bassPc();
        for (size_t i = 1; i < out.size(); ++i)
        {
            auto& c = out[i].chord;
            if (c.isSlash() && ! c.bassIsChordTone()) { prevBass = c.bassPc(); continue; }
            int bestDeg = 1;
            int bestCost = 1 << 20;
            for (int deg : { 1, 3, 5, 7 })
            {
                std::optional<SpelledPitch> tone = deg == 1 ? std::optional<SpelledPitch> (c.root) : chordToneOn (c, deg);
                if (! tone) continue;
                int d = wrapPc (tone->pc() - prevBass);
                d = std::min (d, 12 - d);
                const int cost = (d == 0 ? 6 : (d <= 2 ? d : 3 + d)) + (deg == 1 ? 0 : 1) + (deg == 7 ? 1 : 0);
                if (cost < bestCost) { bestCost = cost; bestDeg = deg; }
            }
            const auto tone = bestDeg == 1 ? std::optional<SpelledPitch> (c.root) : chordToneOn (c, bestDeg);
            c = makeChord (c.root, c.quality, tone);
            prevBass = c.bassPc();
        }
        return out;
    }

    Pattern pushInto (const Pattern& p, const Ctx& cx)
    {
        Pattern out;
        const bool flats = cx.key.signature() <= 0;
        for (size_t i = 0; i < p.size(); ++i)
        {
            const auto& next = p[(i + 1) % p.size()].chord;
            if (p[i].beats < 2.0 || next.root.pc() == p[i].chord.root.pc())
            {
                out.push_back (p[i]);
                continue;
            }
            Chord a;
            if (isDarkUrban (cx.style) || cx.style == Style::Cinematic)
                a = makeChord (tidy (SpelledPitch::above (next.root, 1, 1), flats), ChordQuality::Major);
            else if ((cx.style == Style::NeoSoul || cx.style == Style::Jazz) && cx.mood.tension > 0.55f)
                a = makeChord (tidy (SpelledPitch::above (next.root, 1, 1), flats), ChordQuality::Dominant7);
            else
                a = makeChord (tidy (SpelledPitch::above (next.root, 7, 4), flats),
                               cx.level >= 0.4f ? ChordQuality::Dominant7 : ChordQuality::Major);
            out.push_back ({ p[i].chord, p[i].beats / 2.0 });
            out.push_back ({ a, p[i].beats / 2.0 });
        }
        return out;
    }

    std::optional<Pattern> rotateToLift (const Pattern& p, const Key& key)
    {
        if (p.size() < 2) return std::nullopt;
        size_t best = 0;
        float bestB = -10.0f;
        for (size_t i = 1; i < p.size(); ++i)
        {
            if (p[i].chord.sameHarmony (p[0].chord)) continue;
            const float b = chordBrightness (p[i].chord, key) + (relOf (p[i].chord, key) == 0 ? -0.5f : 0.0f);
            if (b > bestB) { bestB = b; best = i; }
        }
        if (best == 0) return std::nullopt;
        Pattern out;
        for (size_t i = 0; i < p.size(); ++i)
            out.push_back (p[(best + i) % p.size()]);
        return out;
    }

    std::optional<Pattern> stripOf (const Pattern& p, double loopBeats)
    {
        if (p.size() < 2) return std::nullopt;
        const auto& first = p.front().chord;
        const auto& last = p.back().chord;
        if (first.sameHarmony (last)) return Pattern { { first, loopBeats / 2.0 }, { p[1].chord, loopBeats / 2.0 } };
        return Pattern { { first, loopBeats / 2.0 }, { last, loopBeats / 2.0 } };
    }

    std::vector<Candidate> derivedForRole (Section role, const Pattern& backbone, double loopBeats,
                                           const Ctx& cx, double totalBeats, bool backboneIsIdea)
    {
        std::vector<Candidate> out;
        if (backbone.empty()) return out;
        const std::string src = backboneIsIdea ? "your idea" : "the main loop";

        auto add = [&] (Candidate c) { if (! c.body.empty()) out.push_back (std::move (c)); };

        switch (role)
        {
            case Section::Chorus:
            {
                add (derivedCandidate ("loop-color", "Same loop, richer colors",
                                       "Keeps the roots of " + src + " but stacks sevenths and ninths on top, so the section feels fuller without new chords (extensions).",
                                       colorUp (backbone, cx), cx, totalBeats, false));
                if (cx.key.mode == Mode::Minor)
                    add (derivedCandidate ("loop-relative-lift", "Lift to the relative major",
                                           "Swaps the home chord of " + src + " for the relative major, which shares two of its notes, so it lifts while staying familiar (relative major substitution).",
                                           relativeSwap (backbone, cx.key, cx.level), cx, totalBeats, false));
                add (derivedCandidate ("loop-moving-bass", "Same chords, moving bass",
                                       "The chords of " + src + " stay, but inversions make the bass walk by step so the section feels like it is going somewhere (inversions).",
                                       walkingBass (backbone), cx, totalBeats, false));
                add (derivedCandidate ("loop-push", "Push into every change",
                                       "Each chord of " + src + " hands off to an approach chord for the next one, doubling the harmonic rhythm for more drive (secondary dominants and approach chords).",
                                       pushInto (backbone, cx), cx, totalBeats, false));
                if (auto r = rotateToLift (backbone, cx.key))
                    add (derivedCandidate ("loop-rotate", "Same loop, new downbeat",
                                           "Starts " + src + " on " + pop (r->front().chord) + " instead of " + pop (backbone.front().chord)
                                           + ", so the strongest beat lands on a brighter chord (loop rotation).",
                                           *r, cx, totalBeats, false));
                break;
            }
            case Section::Verse:
            {
                if (auto s = stripOf (backbone, loopBeats))
                    add (derivedCandidate ("loop-strip", "Strip it down",
                                           "Keeps just two chords from " + src + " with more space between changes, so the verse sits under the vocal (reduction).",
                                           *s, cx, totalBeats, false));
                add (derivedCandidate ("loop-pedal", "Loop over a held bass",
                                       "Plays " + src + " over a held home note, so the verse hums along and saves the full bass movement for later (tonic pedal).",
                                       pedalOf (backbone, cx.key), cx, totalBeats, false));
                if (auto r = rotateToLift (backbone, cx.key))
                    add (derivedCandidate ("loop-rotate", "Same loop, new downbeat",
                                           "Starts " + src + " on " + pop (r->front().chord) + " so the verse feels related to the chorus but not the same (loop rotation).",
                                           *r, cx, totalBeats, false));
                break;
            }
            case Section::Bridge:
            {
                add (derivedCandidate ("loop-relative", cx.key.mode == Mode::Minor ? "Relative major version of the loop" : "Relative minor version of the loop",
                                       "Turns the home chord of " + src + " into its relative chord, so the bridge sounds familiar but seen from another side (relative substitution).",
                                       relativeSwap (backbone, cx.key, cx.level), cx, totalBeats, false));
                break;
            }
            case Section::Breakdown:
            {
                if (auto s = stripOf (backbone, loopBeats * 2.0))
                    add (derivedCandidate ("loop-strip", "Strip it down",
                                           "Two chords from " + src + " at half the pace give the song room to breathe (reduction).",
                                           *s, cx, totalBeats, false));
                add (derivedCandidate ("loop-pedal", "Loop over a held bass",
                                       "Plays " + src + " over a held home note, so the energy drops while the harmony keeps moving (tonic pedal).",
                                       pedalOf (backbone, cx.key), cx, totalBeats, false));
                break;
            }
            case Section::PostChorus:
            {
                Pattern half (backbone.begin(), backbone.begin() + (std::ptrdiff_t) std::max<size_t> (1, (backbone.size() + 1) / 2));
                add (derivedCandidate ("post-hook-loop", "Loop the first half of the chorus",
                                       "Repeats the opening chords of the chorus as a chant-along tag (post-chorus hook).",
                                       half, cx, totalBeats, false));
                add (derivedCandidate ("post-pedal", "Chorus over a held bass",
                                       "The chorus chords ride over a held home note so the energy stays up while the low end settles (tonic pedal).",
                                       pedalOf (backbone, cx.key), cx, totalBeats, false));
                add (derivedCandidate ("post-color", "Chorus with richer colors",
                                       "Keeps the chorus roots and adds sevenths and ninths for a lush tag (extensions).",
                                       colorUp (backbone, cx), cx, totalBeats, false));
                break;
            }
            default: break;
        }
        return out;
    }

    ChordQuality tonicQuality (const Key& key, float level, Style style)
    {
        const Chord base = tonicChord (key, key.mode == Mode::Minor ? ChordQuality::Minor : ChordQuality::Major);
        return colorize (base, key, level, style).quality;
    }

    std::vector<Candidate> introOutroCandidates (Section role, const Pattern& backbone, double loopBeats, const Ctx& cx,
                                                 double totalBeats, bool backboneIsIdea)
    {
        std::vector<Candidate> out;
        const std::string src = backboneIsIdea ? "your idea" : "the main loop";
        const Chord home = tonicChord (cx.key, tonicQuality (cx.key, cx.level, cx.style));

        auto make = [&] (std::string id, std::string title, std::string why, std::vector<SongChord> body)
        {
            Candidate c;
            c.id = std::move (id);
            c.title = std::move (title);
            c.why = std::move (why);
            c.derived = true;
            mergeRepeats (body);
            c.body = std::move (body);
            c.bright = weightedMean (c.body, [&] (const SongChord& s) { return chordBrightness (s.chord, cx.key); });
            c.tension = weightedMean (c.body, [&] (const SongChord& s) { return chordTension (s.chord, cx.key); });
            out.push_back (std::move (c));
        };

        auto endHome = [&] (std::vector<SongChord> body, const Chord& finalChord)
        {
            // Replace the last bar (or the last chord if shorter) with the final chord.
            const double cut = totalBeats - 4.0;
            std::vector<SongChord> kept;
            for (auto c : body)
            {
                if (c.startBeat >= cut - 1e-9) break;
                c.beats = std::min (c.beats, cut - c.startBeat);
                kept.push_back (c);
            }
            kept.push_back ({ std::max (0.0, cut), totalBeats - std::max (0.0, cut), finalChord, {}, HarmonicFunction::Other });
            return kept;
        };

        if (role == Section::Intro)
        {
            make ("intro-idea", "Start with the loop",
                  "Opens on " + src + " with the arrangement thinned out, so the listener learns the loop before the vocal (intro from the idea).",
                  tile (backbone, totalBeats));
            make ("intro-pedal", "Float over a held bass",
                  "The chords of " + src + " float over a held home note, so the intro hovers before the song really starts (tonic pedal).",
                  tile (pedalOf (backbone, cx.key), totalBeats));
            if (auto s = stripOf (backbone, totalBeats); s && ! backbone.front().chord.sameHarmony (backbone.back().chord))
                make ("intro-hold", "Two-chord teaser",
                      "Holds the first chord, then the last chord of " + src + " turns back into the top of the loop (intro from a subset of the idea).",
                      tile (*s, totalBeats));
            make ("intro-sus", "Open, suspended start",
                  "Opens on a suspended home chord that settles as the song starts, a soft way in (sus2 resolution).",
                  tile ({ { makeChord (cx.key.tonic, ChordQuality::Sus2), totalBeats / 2.0 }, { backbone.front().chord, totalBeats / 2.0 } }, totalBeats));
        }
        else
        {
            const bool picardy = cx.key.mode == Mode::Minor && cx.mood.valence > 0.2f
                              && (cx.style == Style::Gospel || cx.style == Style::Cinematic || cx.style == Style::Pop);
            const Chord finalChord = picardy ? tonicChord (cx.key, cx.level >= 0.5f ? ChordQuality::Major7 : ChordQuality::Major) : home;
            const std::string cadence = picardy ? "Picardy third" : "final cadence";
            make ("outro-resolve", "Resolve home",
                  "Plays " + src + " once more and lands on " + pop (finalChord) + " to close the song (" + cadence + ").",
                  endHome (tile (backbone, totalBeats), finalChord));

            const bool minor = cx.key.mode == Mode::Minor;
            const bool cinematicEnd = cx.style == Style::Cinematic || isDarkUrban (cx.style) || cx.style == Style::EDM || cx.style == Style::Rock;
            Pattern plagal;
            std::string plagalWhy;
            if (cinematicEnd)
            {
                plagal = { { colorize (makeChord (degreePitch (cx.key, 8, 5), ChordQuality::Major), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { colorize (makeChord (degreePitch (cx.key, 10, 6), ChordQuality::Major), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { finalChord, totalBeats / 2.0 } };
                plagalWhy = "The flat six and flat seven climb one last time and stop on home (Aeolian cadence).";
            }
            else if (minor)
            {
                plagal = { { colorize (makeChord (degreePitch (cx.key, 8, 5), ChordQuality::Major), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { colorize (makeChord (degreePitch (cx.key, 5, 3), ChordQuality::Minor), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { finalChord, totalBeats / 2.0 } };
                plagalWhy = "The flat six and the minor four settle onto home like the amen at the end of a hymn (plagal cadence).";
            }
            else
            {
                plagal = { { colorize (makeChord (degreePitch (cx.key, 5, 3), ChordQuality::Major), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { colorize (makeChord (degreePitch (cx.key, 5, 3), ChordQuality::Minor), cx.key, cx.level, cx.style), totalBeats / 4.0 },
                           { finalChord, totalBeats / 2.0 } };
                plagalWhy = "The four chord, then its minor shade, settle onto home like the amen at the end of a hymn (plagal cadence).";
            }
            make (cinematicEnd ? "outro-aeolian" : "outro-plagal",
                  cinematicEnd ? "Climb home and stop" : "Amen ending", plagalWhy, tile (plagal, totalBeats));
            make ("outro-pedal-fade", "Fade over a held bass",
                  "The chords of " + src + " drift over a held home note and fade on " + pop (finalChord) + " (tonic pedal).",
                  endHome (tile (pedalOf (backbone, cx.key), totalBeats), finalChord));
        }
        (void) loopBeats;
        return out;
    }

    unsigned colorsExhibited (const Candidate& c, const Key& key)
    {
        unsigned bits = 0;
        auto set = [&] (ColorFamily f) { bits |= 1u << (int) f; };
        for (const auto& s : c.body)
        {
            const auto rn = RomanNumeral::analyze (s.chord, key);
            if (rn.diatonic) set (ColorFamily::Diatonic);
            if (rn.borrowed) set (ColorFamily::Borrowed);
            if (rn.secondary) set (ColorFamily::SecondaryDominants);
            if (rn.tritoneSub) set (ColorFamily::TritoneSubs);
            const int rel = relOf (s.chord, key);
            if (! rn.diatonic && isMajorish (s.chord.quality) && (rel == 3 || rel == 4 || rel == 8 || rel == 9))
                set (ColorFamily::ChromaticMediants);
            if (familyOf (s.chord.quality) == QualityFamily::Suspended) set (ColorFamily::Suspensions);
            if (theory::info (s.chord.quality).intervals.size() > 3) set (ColorFamily::Extensions);
            if (s.chord.isSlash() && s.chord.bassPc() == key.tonic.pc()) set (ColorFamily::Pedal);
        }
        for (const char* m : { "dorian", "phrygian", "lydian", "mixolydian", "neapolitan", "aeolian" })
            if (c.id.find (m) != std::string::npos)
                set (ColorFamily::Modal);
        return bits;
    }

    // =============================================================================================
    // Builder state
    // =============================================================================================

    struct Built
    {
        SongRequest req;
        Song song;
        std::map<Section, int> firstOf;            // section type -> index of first instance
        Pattern backbone;
        double backboneBeats { 0 };
        bool backboneIsIdea { false };
        Section ideaSection { Section::Verse };
        bool modulationAllowed { false };
    };

    MoodProfile sectionMood (const MoodProfile& global, Section s)
    {
        MoodProfile m = global.resolved (s);
        // The user asked for this section to be happier than the song ("hopeful chorus"): honor it as brightness.
        if (const auto it = global.sections.find (s); it != global.sections.end() && it->second.valence
            && *it->second.valence > global.valence + 0.2f)
        {
            m.valence += 0.15f;
            m.darkness -= 0.15f;
        }
        switch (s)
        {
            case Section::Chorus:
                m.energy += 0.15f;
                if (m.darkness > 0.6f) { m.tension += 0.1f; m.valence += 0.05f; }
                else { m.valence += 0.2f; m.darkness -= 0.1f; }
                break;
            case Section::PreChorus: m.tension += 0.25f; m.energy += 0.05f; break;
            case Section::PostChorus: m.energy += 0.1f; break;
            case Section::Intro: case Section::Outro: m.energy -= 0.15f; break;
            case Section::Breakdown: m.energy -= 0.25f; break;
            default: break;
        }
        m.clampAll();
        return m;
    }

    float desiredBrightness (const MoodProfile& m)
    {
        return std::clamp (0.8f * m.valence - 0.9f * (m.darkness - 0.3f), -1.0f, 1.0f);
    }

    Ctx contextFor (const Built& b, Section s)
    {
        Ctx cx;
        cx.key = b.song.key;
        cx.style = b.song.style;
        cx.mood = sectionMood (b.song.mood, s);
        cx.level = clamp01 (0.55f * cx.mood.sophistication + 0.45f * styleSophistication (cx.style));
        return cx;
    }

    double slotBeatsFor (const Ctx& cx, Section s)
    {
        double cpb = cx.mood.chordsPerBar > 0.0f ? (double) cx.mood.chordsPerBar : styleChordsPerBar (cx.style);
        if (s == Section::Chorus && cx.mood.chordsPerBar <= 0.0f && cx.mood.energy > 0.8f
            && (cx.style == Style::Gospel || cx.style == Style::Pop || cx.style == Style::Afrobeats || cx.style == Style::Country))
            cpb = std::min (2.0, cpb * 2.0);
        if (s == Section::Breakdown || s == Section::Intro)
            cpb = std::max (0.25, cpb * 0.5);
        return 4.0 / cpb;
    }

    std::vector<SectionOption> pickOptions (Built& b, Section s, int index, std::vector<Candidate> cands, Rng& rng)
    {
        const auto& sec = b.song.sections[(size_t) index];
        const Ctx cx = contextFor (b, s);
        const float desB = desiredBrightness (cx.mood);
        const float desE = 0.5f * roleEnergy (s) + 0.5f * cx.mood.energy;

        // Chosen chord sequences of sections built so far (for contrast).
        std::vector<std::vector<std::string>> earlier;
        std::set<int> earlierPcSets;
        std::string verseFirst;
        for (const auto& [type, idx] : b.firstOf)
        {
            const auto& other = b.song.sections[(size_t) idx];
            if (idx == index || other.options.empty()) continue;
            earlier.push_back (plainSymbols (other.current().body));
            for (const auto& c : other.current().body)
            {
                int mask = 0;
                for (int pc : c.chord.allPcs()) mask |= 1 << pc;
                earlierPcSets.insert (mask);
            }
            if (type == Section::Verse)
                verseFirst = pop (other.current().body.front().chord);
        }

        // The section that follows this one, when it already has chords: reward endings that lead into it.
        std::optional<Chord> nextFirst;
        if (index + 1 < (int) b.song.sections.size())
        {
            const auto& nx = b.song.sections[(size_t) index + 1];
            const auto& src = nx.repeatOf >= 0 ? b.song.sections[(size_t) nx.repeatOf] : nx;
            if (! src.options.empty() && ! src.current().body.empty())
                nextFirst = src.current().body.front().chord;
        }

        for (auto& c : cands)
        {
            float sc = 0.0f;
            if (nextFirst && ! c.body.empty())
            {
                const auto t = transitionBetween (c.body.back().chord, *nextFirst);
                sc += isStrongTransition (t) ? 0.4f : (t == Transition::CommonTone ? 0.15f : 0.0f);
            }
            if (c.derived) sc += 0.6f;
            else if (c.styles == 0) sc += 0.3f;
            else sc += (c.styles & (1u << (int) b.song.style)) ? 1.0f : -0.7f;

            sc -= 1.3f * std::fabs (c.bright - desB);
            sc -= 0.7f * std::fabs (c.tension - cx.mood.tension);
            sc -= 0.3f * std::fabs (c.energy - desE);

            const unsigned ex = colorsExhibited (c, cx.key);
            for (auto f : cx.mood.colors)
                if (ex & (1u << (int) f)) sc += 0.25f;

            const auto syms = plainSymbols (c.body);
            for (const auto& e : earlier)
                if (e == syms) sc -= 1.5f;

            if (s == Section::Chorus && ! verseFirst.empty() && ! c.body.empty() && pop (c.body.front().chord) == verseFirst)
                sc -= 0.25f;

            if (s == Section::Bridge && ! c.body.empty())
            {
                int shared = 0;
                for (const auto& ch : c.body)
                {
                    int mask = 0;
                    for (int pc : ch.chord.allPcs()) mask |= 1 << pc;
                    if (earlierPcSets.count (mask)) ++shared;
                }
                sc += 0.6f * (1.0f - (float) shared / (float) c.body.size()) - 0.3f;
            }

            if (c.derived && b.req.ideaRole == IdeaRole::Loop && b.backboneIsIdea)
                sc += (s == Section::Chorus) ? 1.2f : 0.4f;

            if (c.keyShift != 0)
                sc += 0.1f;   // a key change is a strong bridge move when the style allows it

            sc += (rng.uniform() - 0.5f) * 0.6f;
            c.score = sc;
        }

        std::stable_sort (cands.begin(), cands.end(), [] (const Candidate& x, const Candidate& y) { return x.score > y.score; });

        const int want = std::clamp (b.req.variationsPerSection, 1, 6);
        std::vector<Candidate> picked;
        std::vector<bool> taken (cands.size(), false);
        while ((int) picked.size() < want)
        {
            int best = -1;
            float bestScore = -1e9f;
            for (size_t i = 0; i < cands.size(); ++i)
            {
                if (taken[i] || cands[i].body.empty()) continue;
                const auto syms = symbolsOf (cands[i].body);
                bool dup = false;
                float adj = cands[i].score;
                for (const auto& p : picked)
                {
                    if (symbolsOf (p.body) == syms || p.id == cands[i].id) dup = true;
                    if (pop (p.body.front().chord) == pop (cands[i].body.front().chord)) adj -= 0.35f;
                    if (p.title == cands[i].title) adj -= 0.5f;
                }
                if (dup) { taken[i] = true; continue; }
                if (adj > bestScore) { bestScore = adj; best = (int) i; }
            }
            if (best < 0) break;
            taken[(size_t) best] = true;
            picked.push_back (cands[(size_t) best]);
        }

        std::vector<SectionOption> out;
        for (auto& c : picked)
        {
            SectionOption o;
            o.id = c.id;
            o.title = c.title;
            o.why = c.why;
            o.colorNote = c.colorNote;
            o.body = c.body;
            o.chords = c.body;
            o.bodyKey = b.song.key;
            o.keyShiftAfter = c.keyShift == 0 ? 0
                            : ((b.song.style == Style::Gospel || b.song.style == Style::RnB || b.song.style == Style::NeoSoul) ? 1 : 2);
            o.energy = clamp01 (0.5f * roleEnergy (s) + 0.5f * cx.mood.energy + (c.energy - 0.5f) * 0.15f);
            o.tension = clamp01 (0.6f * c.tension + 0.4f * cx.mood.tension);
            out.push_back (std::move (o));
        }
        (void) sec;
        return out;
    }

    std::vector<Candidate> templateCandidates (const Built& b, Section s, double totalBeats)
    {
        const Ctx cx = contextFor (b, s);
        const double slot = slotBeatsFor (cx, s);
        R role = R::Verse;
        switch (s)
        {
            case Section::Chorus: case Section::PostChorus: role = R::Chorus; break;
            case Section::PreChorus: role = R::Pre; break;
            case Section::Bridge: role = R::Bridge; break;
            default: role = R::Verse; break;
        }
        const bool minor = b.song.key.mode == Mode::Minor;
        std::vector<Candidate> out;
        for (const auto& t : templates())
        {
            if (t.role != role || t.minor != minor) continue;
            if (t.keyShift != 0 && ! (s == Section::Bridge && b.modulationAllowed)) continue;
            out.push_back (fromTemplate (t, cx, slot, totalBeats));
        }
        return out;
    }

    void generateSection (Built& b, Section s, Rng& rng)
    {
        const auto it = b.firstOf.find (s);
        if (it == b.firstOf.end()) return;
        const int index = it->second;
        auto& sec = b.song.sections[(size_t) index];
        if (sec.isIdea) return;
        const double total = sec.bars * 4.0;
        const Ctx cx = contextFor (b, s);

        std::vector<Candidate> cands;
        if (s == Section::Intro || s == Section::Outro)
            cands = introOutroCandidates (s, b.backbone, b.backboneBeats, cx, total, b.backboneIsIdea);
        else
        {
            cands = templateCandidates (b, s, total);
            Pattern source = b.backbone;
            double sourceBeats = b.backboneBeats;
            bool isIdea = b.backboneIsIdea;
            if (s == Section::PostChorus)
            {
                if (auto c = b.firstOf.find (Section::Chorus); c != b.firstOf.end() && ! b.song.sections[(size_t) c->second].options.empty())
                {
                    const auto& body = b.song.sections[(size_t) c->second].current().body;
                    const double beats = body.empty() ? 0.0 : body.back().startBeat + body.back().beats;
                    source = toPattern (body, beats);
                    sourceBeats = beats;
                    isIdea = false;
                }
            }
            auto d = derivedForRole (s, source, sourceBeats, cx, total, isIdea);
            cands.insert (cands.end(), d.begin(), d.end());
        }

        Rng local { rng.next() ^ hashString (sec.label) };
        sec.options = pickOptions (b, s, index, std::move (cands), local);
        sec.chosen = 0;
    }

    // =============================================================================================
    // Linking
    // =============================================================================================

    std::vector<SongChord> retile (const std::vector<SongChord>& body, double srcBeats, double dstBeats)
    {
        if (std::fabs (srcBeats - dstBeats) < 1e-9) return body;
        auto out = tile (toPattern (body, srcBeats), dstBeats);
        mergeRepeats (out);
        return out;
    }

    std::string chordList (const std::vector<SongChord>& v)
    {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 8; ++i)
            s += (i ? ", " : "") + pop (v[i].chord);
        return s;
    }

    std::string buildArc (const Song& song)
    {
        std::string a = "The song sits in " + song.key.toString() + " at " + std::to_string ((int) std::lround (song.bpm))
                      + " BPM with " + (song.style == Style::RnB || song.style == Style::EDM || song.style == Style::Afrobeats ? "an " : "a ")
                      + styleName (song.style) + " feel and grows out of your idea.";
        std::set<Section> seen;
        float verseE = -1, chorusE = -1;
        bool keyUp = false;
        for (const auto& s : song.sections)
        {
            if (s.section == Section::Verse && verseE < 0) verseE = s.current().energy;
            if (s.section == Section::Chorus) chorusE = std::max (chorusE, s.current().energy);
            if (s.current().keyShiftAfter != 0) keyUp = true;
            if (! seen.insert (s.section).second) continue;
            std::string name = s.label;
            if (const auto sp = name.rfind (' '); sp != std::string::npos && std::isdigit ((unsigned char) name[sp + 1]))
                name = name.substr (0, sp);
            a += " " + name + ": " + (s.isIdea ? std::string ("your idea, exactly as played") : lowerFirst (s.current().title)) + ".";
        }
        if (verseE >= 0 && chorusE >= 0)
        {
            char buf[96];
            std::snprintf (buf, sizeof (buf), " Energy climbs from %.2f in the verse to %.2f in the chorus", (double) verseE, (double) chorusE);
            a += buf;
            a += keyUp ? ", the last chorus jumps up a key, and the outro resolves home." : ", and the outro resolves home.";
        }
        return a;
    }
}

// =================================================================================================
// Public
// =================================================================================================

int Song::totalBars() const
{
    int n = 0;
    for (const auto& s : sections) n += s.bars;
    return n;
}

std::string_view toString (Transition t)
{
    switch (t)
    {
        case Transition::None: return "None";
        case Transition::CommonTone: return "CommonTone";
        case Transition::Stepwise: return "Stepwise";
        case Transition::Plagal: return "Plagal";
        case Transition::Backdoor: return "Backdoor";
        case Transition::HalfStepAbove: return "HalfStepAbove";
        case Transition::Dominant: return "Dominant";
    }
    return "None";
}

Transition transitionBetween (const Chord& from, const Chord& to)
{
    const int d = wrapPc (from.root.pc() - to.root.pc());
    const auto fam = familyOf (from.quality);
    const bool dominantShape = fam == QualityFamily::Dominant || fam == QualityFamily::Major || fam == QualityFamily::Suspended;
    if (d == 7 && (dominantShape || fam == QualityFamily::Minor)) return Transition::Dominant;
    if (d == 1 && (dominantShape || fam == QualityFamily::Diminished)) return Transition::HalfStepAbove;
    if (d == 11 && fam == QualityFamily::Diminished) return Transition::Dominant;   // leading-tone diminished
    if (d == 10 && dominantShape) return Transition::Backdoor;
    if (d == 5 && (dominantShape || fam == QualityFamily::Minor)) return Transition::Plagal;
    if (sharesTone (from, to)) return Transition::CommonTone;
    const int bd = wrapPc (from.bassPc() - to.bassPc());
    if (bd == 1 || bd == 2 || bd == 10 || bd == 11) return Transition::Stepwise;
    return Transition::None;
}

bool isStrongTransition (Transition t)
{
    return t == Transition::Dominant || t == Transition::HalfStepAbove || t == Transition::Backdoor || t == Transition::Plagal;
}

Song SongBuilder::build (const SongRequest& request)
{
    Built b;
    b.req = request;
    Song& song = b.song;

    MoodProfile mood = request.mood;
    mood.clampAll();
    song.style = resolveStyle (mood);
    mood.style = song.style;
    song.mood = mood;
    song.ideaRole = request.ideaRole;

    // ---- the heard idea
    std::vector<analysis::ChordSegment> segs = request.idea.segments;
    std::stable_sort (segs.begin(), segs.end(), [] (const auto& x, const auto& y) { return x.startBeat < y.startBeat; });
    const bool haveIdea = ! segs.empty();

    if (haveIdea)
        song.key = request.idea.key;
    else
    {
        const bool minor = mood.darkness > 0.5f || mood.valence < -0.15f || song.style == Style::Drill || song.style == Style::Trap;
        song.key = { request.idea.key.tonic, minor ? Mode::Minor : Mode::Major };
    }
    if (request.bpm && *request.bpm > 0.0) song.bpm = *request.bpm;
    else if (haveIdea && request.idea.grid.bpm > 0.0) song.bpm = request.idea.grid.bpm;
    else song.bpm = styleBpm (song.style);
    song.beatsPerBar = 4;

    // ---- structure
    song.structure = request.structure == StructurePreset::Auto ? structureForStyle (song.style) : request.structure;
    auto specs = structureSections (request.structure, song.style, request.customStructure);
    if (request.structure == StructurePreset::Custom && request.customStructure.empty())
        song.structure = structureForStyle (song.style);

    Section ideaType = request.ideaRole == IdeaRole::Chorus || request.ideaRole == IdeaRole::Hook ? Section::Chorus : Section::Verse;
    if (std::none_of (specs.begin(), specs.end(), [&] (const SectionSpec& s) { return s.section == ideaType; }))
    {
        const Section fallback[] = { Section::Breakdown, Section::Chorus, Section::Verse, Section::PreChorus, Section::Bridge, Section::PostChorus };
        for (auto f : fallback)
            if (std::any_of (specs.begin(), specs.end(), [&] (const SectionSpec& s) { return s.section == f; })) { ideaType = f; break; }
        if (ideaType == Section::Intro || ideaType == Section::Outro || std::none_of (specs.begin(), specs.end(), [&] (const SectionSpec& s) { return s.section == ideaType; }))
            ideaType = specs.empty() ? Section::Verse : specs.front().section;
    }
    b.ideaSection = ideaType;

    // Idea timing: first chord at beat 0, length rounded up to whole bars.
    std::vector<SongChord> ideaBody;
    double ideaBeats = 0.0;
    if (haveIdea)
    {
        const double origin = segs.front().startBeat;
        for (const auto& s : segs)
            ideaBody.push_back ({ s.startBeat - origin, s.beats, s.chord, {}, HarmonicFunction::Other });
        const double end = ideaBody.back().startBeat + ideaBody.back().beats;
        ideaBeats = std::max (4.0, std::ceil (end / 4.0 - 1e-9) * 4.0);
    }

    // Labels and sections.
    std::map<std::string, int> nameCount, nameSeen;
    for (const auto& s : specs) ++nameCount[s.name];
    for (const auto& spec : specs)
    {
        SongSection sec;
        sec.section = spec.section;
        sec.bars = spec.bars;
        sec.label = nameCount[spec.name] > 1 ? spec.name + " " + std::to_string (++nameSeen[spec.name]) : spec.name;
        sec.key = song.key;
        sec.isIdea = haveIdea && spec.section == ideaType;
        if (sec.isIdea)
            sec.bars = std::max (sec.bars, (int) std::lround (ideaBeats / 4.0));
        const int idx = (int) song.sections.size();
        if (b.firstOf.count (spec.section)) sec.repeatOf = b.firstOf[spec.section];
        else b.firstOf[spec.section] = idx;
        song.sections.push_back (std::move (sec));
    }
    if (song.sections.empty())
        return song;

    // Key changes: only into a later Chorus, never across the heard idea, and only in styles that use them.
    {
        const bool styleOk = song.style == Style::Pop || song.style == Style::Gospel || song.style == Style::RnB
                          || song.style == Style::Rock || song.style == Style::Country || song.style == Style::Cinematic
                          || song.style == Style::NeoSoul;
        bool ok = false;
        for (size_t i = 0; i < song.sections.size(); ++i)
            if (song.sections[i].section == Section::Bridge)
            {
                bool chorusAfter = false, ideaAfter = false;
                for (size_t j = i + 1; j < song.sections.size(); ++j)
                {
                    chorusAfter = chorusAfter || song.sections[j].section == Section::Chorus;
                    ideaAfter = ideaAfter || song.sections[j].isIdea;
                }
                ok = chorusAfter && ! ideaAfter && song.sections[i].repeatOf < 0;
            }
        b.modulationAllowed = styleOk && ok;
    }

    Rng rng { request.seed ^ 0xA5A5F00DCAFEull };

    // ---- the idea section (exact) or a generated backbone
    if (haveIdea)
    {
        auto& sec = song.sections[(size_t) b.firstOf[ideaType]];
        SectionOption o;
        o.id = "idea";
        o.title = "Your idea";
        o.why = "This is your idea, kept exactly as you played it (" + chordList (ideaBody) + ").";
        Pattern p = toPattern (ideaBody, ideaBeats);
        // Tile the exact chords (and their exact lengths) across the section.
        std::vector<SongChord> body;
        for (double off = 0.0; off < sec.bars * 4.0 - 1e-9; off += ideaBeats)
            for (const auto& c : ideaBody)
            {
                if (off + c.startBeat >= sec.bars * 4.0 - 1e-9) break;
                auto cc = c;
                cc.startBeat += off;
                cc.beats = std::min (cc.beats, sec.bars * 4.0 - cc.startBeat);
                body.push_back (cc);
            }
        o.body = body;
        o.chords = body;
        o.bodyKey = song.key;
        sec.options = { o };
        b.backbone = p;
        b.backboneBeats = ideaBeats;
        b.backboneIsIdea = true;
    }
    else
    {
        generateSection (b, ideaType, rng);
        const auto& body = song.sections[(size_t) b.firstOf[ideaType]].current().body;
        const double beats = body.empty() ? 16.0 : body.back().startBeat + body.back().beats;
        // The backbone is the first loop of the generated section (up to 4 chords or 4 bars).
        std::vector<SongChord> loop;
        for (const auto& c : body)
            if (c.startBeat < 16.0 - 1e-9 && loop.size() < 4) loop.push_back (c);
        const double loopBeats = loop.empty() ? beats : std::min (beats, loop.back().startBeat + loop.back().beats);
        b.backbone = toPattern (loop, loopBeats);
        b.backboneBeats = loopBeats;
        b.backboneIsIdea = false;
    }

    // ---- everything else, in an order where later sections can contrast with earlier ones
    const Section order[] = { Section::Chorus, Section::Verse, Section::PreChorus, Section::Bridge,
                              Section::PostChorus, Section::Breakdown, Section::Intro, Section::Outro };
    for (auto s : order)
        if (s != ideaType || ! haveIdea)
            if (b.firstOf.count (s) && song.sections[(size_t) b.firstOf[s]].options.empty())
                generateSection (b, s, rng);

    relink (song);
    return song;
}

void SongBuilder::choose (Song& song, int sectionIndex, int optionIndex)
{
    if (sectionIndex < 0 || sectionIndex >= (int) song.sections.size()) return;
    int src = song.sections[(size_t) sectionIndex].repeatOf >= 0 ? song.sections[(size_t) sectionIndex].repeatOf : sectionIndex;
    auto& s = song.sections[(size_t) src];
    if (optionIndex < 0 || optionIndex >= (int) s.options.size()) return;
    s.chosen = optionIndex;
    relink (song);
}

void SongBuilder::relink (Song& song)
{
    auto& secs = song.sections;
    if (secs.empty()) return;

    // 1. Keys, repeats and start bars.
    Key running = song.key;
    int bar = 0;
    for (size_t i = 0; i < secs.size(); ++i)
    {
        auto& s = secs[i];
        s.key = running;
        s.startBar = bar;
        if (s.repeatOf >= 0)
        {
            const auto& src = secs[(size_t) s.repeatOf];
            if (s.isIdea) s.bars = std::max (s.bars, src.bars);
            s.options = src.options;
            s.chosen = src.chosen;
            for (auto& o : s.options)
                o.body = retile (o.body, src.bars * 4.0, s.bars * 4.0);
        }
        for (auto& o : s.options)
        {
            if (! (o.bodyKey == s.key))
            {
                for (auto& c : o.body)
                    c.chord = transposeChord (c.chord, o.bodyKey, s.key);
                o.bodyKey = s.key;
            }
            o.chords = o.body;
        }
        bar += s.bars;
        if (s.current().keyShiftAfter != 0)
            running = shiftedKey (running, s.current().keyShiftAfter);
    }

    // 2. Transitions and explanations.
    for (size_t i = 0; i < secs.size(); ++i)
    {
        auto& s = secs[i];
        const bool last = i + 1 == secs.size();
        Ctx cx;
        cx.key = s.key;
        cx.style = song.style;
        cx.mood = sectionMood (song.mood, s.section);
        cx.level = clamp01 (0.55f * cx.mood.sophistication + 0.45f * styleSophistication (song.style));

        for (auto& o : s.options)
        {
            std::string sentence;
            if (last || o.body.empty())
            {
                o.chords = o.body;
                if (! o.chords.empty() && relOf (o.chords.back().chord, s.key) == 0)
                    sentence = "It ends on the home chord, " + pop (o.chords.back().chord) + ", so the song closes resolved (final cadence).";
            }
            else
            {
                const auto& next = secs[i + 1];
                Chord target = next.current().body.front().chord;
                bool modulating = ! (next.key == s.key);
                if (o.keyShiftAfter != 0 && &o != &s.current())
                {
                    // Describe and set up the key change this option would make if it were picked.
                    const Key shifted = shiftedKey (s.key, o.keyShiftAfter);
                    target = transposeChord (target, next.key, shifted);
                    modulating = true;
                }
                else if (o.keyShiftAfter == 0 && &o != &s.current() && modulating)
                {
                    // The chosen option changes key, this one would not.
                    target = transposeChord (target, next.key, s.key);
                    modulating = false;
                }
                if (s.isIdea)
                {
                    o.chords = o.body;
                    const auto t = transitionBetween (o.chords.back().chord, target);
                    sentence = "Its last chord, " + pop (o.chords.back().chord) + ", leads into " + pop (target)
                             + " at the top of the " + next.label + " (" + transitionPhrase (t) + ").";
                }
                else
                    o.chords = applyTransition (o.body, target, cx, modulating, sentence, next.label);
            }

            for (auto& c : o.chords)
            {
                const auto rn = RomanNumeral::analyze (c.chord, s.key);
                c.roman = rn.numeral;
                c.function = rn.function;
            }
            for (auto& c : o.body)
            {
                const auto rn = RomanNumeral::analyze (c.chord, s.key);
                c.roman = rn.numeral;
                c.function = rn.function;
            }

            o.explanation = o.why;
            if (! o.colorNote.empty()) o.explanation += " " + o.colorNote;
            if (! sentence.empty()) o.explanation += " " + sentence;

            const float t = weightedMean (o.chords, [&] (const SongChord& c) { return chordTension (c.chord, s.key); });
            o.tension = clamp01 (0.6f * t + 0.4f * cx.mood.tension);
            if (s.isIdea)
                o.energy = clamp01 (0.5f * roleEnergy (s.section) + 0.5f * cx.mood.energy);
        }
    }

    // 3. Variation flags on repeats.
    for (auto& s : secs)
    {
        s.variation = false;
        s.variationNote.clear();
        if (s.repeatOf < 0) continue;
        const auto& src = secs[(size_t) s.repeatOf];
        const bool keyChanged = ! (s.key == src.key);
        const bool differs = plainSymbols (s.current().chords) != plainSymbols (src.current().chords);
        if (keyChanged)
        {
            s.variation = true;
            s.variationNote = "Same as " + src.label + ", moved up to " + s.key.toString() + " for the lift.";
        }
        else if (differs)
        {
            s.variation = true;
            s.variationNote = "Same as " + src.label + " with a different last bar to lead into what comes next.";
        }
        else
            s.variationNote = "Same as " + src.label + ".";
    }

    song.arc = buildArc (song);
}

} // namespace so::song
