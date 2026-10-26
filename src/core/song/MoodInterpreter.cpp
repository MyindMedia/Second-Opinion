#include "MoodInterpreter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

namespace so::song
{

namespace
{
    struct LexEntry
    {
        const char* phrase;
        float v, e, t, d, s;
        Style style = Style::Auto;
        float styleWeight = 0.0f;
        unsigned colors = 0;
        float chordsPerBar = 0.0f;
    };

    const std::vector<LexEntry>& lexicon()
    {
        static const std::vector<LexEntry> table {
#include "MoodLexicon.inc"
        };
        return table;
    }

    using Lookup = std::unordered_map<std::string, size_t>;

    const Lookup& lookup()
    {
        static const Lookup map = []
        {
            Lookup m;
            const auto& lx = lexicon();
            for (size_t i = 0; i < lx.size(); ++i)
                m.emplace (lx[i].phrase, i);   // first definition wins
            return m;
        }();
        return map;
    }

    /** Section phrases, longest first so "pre chorus" wins over "chorus". */
    struct SectionWord { const char* phrase; Section section; };
    constexpr std::array<SectionWord, 33> sectionWords {{
        { "pre chorus", Section::PreChorus }, { "pre choruses", Section::PreChorus },
        { "pre hook", Section::PreChorus },   { "build up", Section::PreChorus },
        { "post chorus", Section::PostChorus }, { "post hook", Section::PostChorus },
        { "middle 8", Section::Bridge },      { "middle eight", Section::Bridge },
        { "prechorus", Section::PreChorus },  { "prehook", Section::PreChorus },
        { "buildup", Section::PreChorus },    { "build", Section::PreChorus },
        { "pre", Section::PreChorus },        { "postchorus", Section::PostChorus },
        { "verse", Section::Verse },          { "verses", Section::Verse },
        { "chorus", Section::Chorus },        { "choruses", Section::Chorus },
        { "hook", Section::Chorus },          { "hooks", Section::Chorus },
        { "refrain", Section::Chorus },       { "drop", Section::Chorus },
        { "drops", Section::Chorus },         { "bridge", Section::Bridge },
        { "bridges", Section::Bridge },       { "intro", Section::Intro },
        { "opening", Section::Intro },        { "outro", Section::Outro },
        { "ending", Section::Outro },         { "ends", Section::Outro },
        { "breakdown", Section::Breakdown },  { "break down", Section::Breakdown },
        { "interlude", Section::Breakdown },
    }};

    bool isNegator (const std::string& w)
    {
        static const std::array<const char*, 14> words { "not", "no", "never", "without", "less", "isnt",
            "dont", "doesnt", "aint", "avoid", "nothing", "zero", "minus", "neither" };
        return std::any_of (words.begin(), words.end(), [&] (const char* x) { return w == x; });
    }

    /** Multiplier for a single-word intensity modifier, or 0 when `w` is not one. */
    float intensityOf (const std::string& w)
    {
        static const std::array<std::pair<const char*, float>, 30> words {{
            { "very", 1.5f }, { "really", 1.4f }, { "super", 1.6f }, { "extremely", 1.8f },
            { "so", 1.3f }, { "hella", 1.6f }, { "mad", 1.5f }, { "ultra", 1.7f },
            { "deeply", 1.5f }, { "incredibly", 1.7f }, { "totally", 1.5f }, { "fully", 1.4f },
            { "real", 1.3f }, { "most", 1.5f }, { "more", 1.25f }, { "extra", 1.5f }, { "insanely", 1.8f },
            { "slightly", 0.5f }, { "kinda", 0.6f }, { "somewhat", 0.6f }, { "sorta", 0.6f },
            { "lightly", 0.5f }, { "mildly", 0.5f }, { "barely", 0.3f }, { "subtly", 0.5f },
            { "touch", 0.5f }, { "hint", 0.4f }, { "bit", 0.5f }, { "little", 0.5f }, { "semi", 0.6f },
        }};
        for (const auto& [word, m] : words)
            if (w == word)
                return m;
        return 0.0f;
    }

    bool isClauseBreakWord (const std::string& w)
    {
        return w == "but" || w == "then" || w == "while" || w == "whereas" || w == "except"
            || w == "though" || w == "although" || w == "yet" || w == "however";
    }

    struct Token { std::string text; int clause; };

    std::vector<Token> tokenize (std::string_view text)
    {
        std::vector<Token> out;
        std::string cur;
        int clause = 0;

        auto flush = [&]
        {
            if (cur.empty()) return;
            if (isClauseBreakWord (cur)) ++clause;
            else out.push_back ({ cur, clause });
            cur.clear();
        };

        for (size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = (unsigned char) text[i];
            if (c >= 'A' && c <= 'Z')                       cur += (char) (c - 'A' + 'a');
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) cur += (char) c;
            else if (c == '&')
            {
                // "r&b" stays one token; a free-standing "&" becomes its own token ("r & b").
                if (! cur.empty()) cur += '&';
                else { out.push_back ({ "&", clause }); }
            }
            else if (c == '\'' )                            continue;   // isn't -> isnt, '90s -> 90s
            else if (c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' || c == '\n' || c == '(' || c == ')')
            {
                flush();
                ++clause;
            }
            else
                flush();   // spaces, dashes, slashes, anything else
        }
        flush();
        return out;
    }

    std::string join (const std::vector<Token>& toks, size_t from, size_t count)
    {
        std::string s;
        for (size_t i = 0; i < count; ++i)
            s += (i ? " " : "") + toks[from + i].text;
        return s;
    }

    /** Light stemming fallback for single words not in the lexicon (darker -> dark, sadly -> sad). */
    std::optional<size_t> stemLookup (const std::string& w)
    {
        const auto& m = lookup();
        static const std::array<const char*, 9> suffixes { "est", "er", "ly", "ness", "ing", "ed", "es", "s", "y" };
        for (const char* suf : suffixes)
        {
            const std::string sfx (suf);
            if (w.size() <= sfx.size() + 2 || w.compare (w.size() - sfx.size(), sfx.size(), sfx) != 0)
                continue;
            std::string base = w.substr (0, w.size() - sfx.size());
            for (const auto& cand : { base, base + "e", base + "y",
                                      base.size() > 2 && base[base.size() - 1] == base[base.size() - 2] ? base.substr (0, base.size() - 1) : base })
                if (auto it = m.find (cand); it != m.end())
                    return it->second;
            if (base.size() > 1 && base.back() == 'i')
                if (auto it = m.find (base.substr (0, base.size() - 1) + "y"); it != m.end())
                    return it->second;   // happier -> happy, gloomily -> gloomy
        }
        return std::nullopt;
    }

    struct Hit
    {
        size_t entry;
        size_t pos;      // first token index
        size_t len;
        float weight;    // signed
        bool negated;
    };

    struct SectionHit { size_t pos; Section section; };

    struct Accum
    {
        float v = 0, e = 0, t = 0, d = 0, s = 0;
        bool touched[5] {};
        unsigned colors = 0;
        float cpb = 0.0f;

        void add (const LexEntry& x, float w)
        {
            const float vals[5] { x.v, x.e, x.t, x.d, x.s };
            float* dst[5] { &v, &e, &t, &d, &s };
            for (int i = 0; i < 5; ++i)
            {
                *dst[i] += vals[i] * w;
                if (vals[i] != 0.0f) touched[i] = true;
            }
            if (w > 0.0f)
            {
                colors |= x.colors;
                if (x.chordsPerBar > 0.0f) cpb = x.chordsPerBar;
            }
        }
    };

    std::vector<ColorFamily> colorsFromBits (unsigned bits)
    {
        std::vector<ColorFamily> out;
        for (int i = 0; i < numColorFamilies; ++i)
            if (bits & (1u << i))
                out.push_back ((ColorFamily) i);
        return out;
    }
}

size_t MoodInterpreter::lexiconSize() { return lexicon().size(); }

MoodReading MoodInterpreter::read (std::string_view description, const std::vector<MoodChip>& chips)
{
    MoodReading reading;
    const auto& lx = lexicon();
    const auto& map = lookup();
    const auto toks = tokenize (description);

    Accum global;
    std::map<Section, Accum> perSection;
    std::array<float, numStyles> styleVotes {};
    std::array<int, numStyles> styleFirstSeen {};
    styleFirstSeen.fill (1 << 30);
    int order = 0;

    auto voteStyle = [&] (const LexEntry& x, float w)
    {
        if (x.style == Style::Auto || w <= 0.0f) return;
        styleVotes[(size_t) x.style] += x.styleWeight;
        styleFirstSeen[(size_t) x.style] = std::min (styleFirstSeen[(size_t) x.style], order++);
    };

    // 1. Find section words and lexicon hits (longest match first).
    std::vector<SectionHit> sectionHits;
    std::vector<Hit> hits;
    std::vector<bool> used (toks.size(), false);

    for (size_t i = 0; i < toks.size();)
    {
        bool matched = false;
        for (size_t len = 3; len >= 1 && ! matched; --len)
        {
            if (i + len > toks.size()) continue;
            bool sameClause = true;
            for (size_t k = 1; k < len; ++k)
                sameClause = sameClause && toks[i + k].clause == toks[i].clause;
            if (! sameClause) continue;
            const auto phrase = join (toks, i, len);
            for (const auto& sw : sectionWords)
                if (phrase == sw.phrase)
                {
                    // "build" or "drop" next to a mood word ("builds up", "drop the energy") is still a section here;
                    // only the exact section phrases above count.
                    sectionHits.push_back ({ i, sw.section });
                    for (size_t k = 0; k < len; ++k) used[i + k] = true;
                    i += len;
                    matched = true;
                    break;
                }
        }
        if (! matched) ++i;
    }

    for (size_t i = 0; i < toks.size();)
    {
        if (used[i]) { ++i; continue; }
        std::optional<size_t> entry;
        size_t len = 0;
        for (size_t l = 4; l >= 1; --l)
        {
            if (i + l > toks.size()) continue;
            bool ok = true;
            for (size_t k = 0; k < l; ++k)
                ok = ok && ! used[i + k] && toks[i + k].clause == toks[i].clause;
            if (! ok) continue;
            if (auto it = map.find (join (toks, i, l)); it != map.end()) { entry = it->second; len = l; break; }
        }
        if (! entry)
            if (auto st = stemLookup (toks[i].text)) { entry = st; len = 1; }

        if (! entry) { ++i; continue; }

        // Modifiers: look back up to 3 tokens in the same clause, stopping at another hit.
        float intensity = 1.0f;
        bool negated = false;
        for (size_t back = 1; back <= 3 && back <= i; ++back)
        {
            const auto& t = toks[i - back];
            if (t.clause != toks[i].clause || used[i - back]) break;
            if (! hits.empty() && hits.back().pos + hits.back().len > i - back) break;
            if (isNegator (t.text)) negated = true;
            else if (const float m = intensityOf (t.text); m > 0.0f && intensity == 1.0f) intensity = m;
        }

        const float w = negated ? -0.5f * intensity : intensity;
        hits.push_back ({ *entry, i, len, w, negated });
        for (size_t k = 0; k < len; ++k) used[i + k] = true;
        i += len;
    }

    // 2. Bind every hit to the nearest section word in its clause (ties go to the following word).
    for (const auto& h : hits)
    {
        const auto& x = lx[h.entry];
        std::optional<Section> bound;
        long best = 1L << 30;
        for (const auto& sh : sectionHits)
        {
            if (toks[sh.pos].clause != toks[h.pos].clause) continue;
            long dist = std::labs ((long) sh.pos - (long) h.pos);
            // "and" / "or" between the word and the section starts a new noun phrase: weaker binding.
            const size_t lo = std::min (sh.pos, h.pos), hi = std::max (sh.pos, h.pos);
            for (size_t k = lo + 1; k < hi; ++k)
                if (toks[k].text == "and" || toks[k].text == "or" || toks[k].text == "plus") { dist += 2; break; }
            if (dist < best || (dist == best && sh.pos > h.pos)) { best = dist; bound = sh.section; }
        }

        // Genre words always describe the whole song.
        if (x.style != Style::Auto && x.styleWeight >= 0.9f)
            bound.reset();

        voteStyle (x, h.weight);
        if (bound) perSection[*bound].add (x, h.weight);
        else global.add (x, h.weight);

        reading.terms.push_back ({ x.phrase, bound, h.weight, h.negated, false });
    }

    // 3. Mood chips apply to the whole song.
    for (auto chip : chips)
    {
        std::string name (toString (chip));
        for (auto& c : name) c = (char) ((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        if (auto it = map.find (name); it != map.end())
        {
            const auto& x = lx[it->second];
            global.add (x, 1.0f);
            if (x.style != Style::Auto)
            {
                styleVotes[(size_t) x.style] += 0.4f;
                styleFirstSeen[(size_t) x.style] = std::min (styleFirstSeen[(size_t) x.style], order++);
            }
            reading.terms.push_back ({ x.phrase, std::nullopt, 1.0f, false, true });
        }
    }

    // 4. Build the profile.
    MoodProfile& p = reading.profile;
    const MoodProfile base;
    p.valence = base.valence + global.v;
    p.energy = base.energy + global.e;
    p.tension = base.tension + global.t;
    p.darkness = base.darkness + global.d;
    p.sophistication = base.sophistication + global.s;
    p.chordsPerBar = global.cpb;
    p.colors = colorsFromBits (global.colors);

    int bestStyle = 0;
    for (int i = 1; i < numStyles; ++i)
        if (styleVotes[(size_t) i] > styleVotes[(size_t) bestStyle]
            || (styleVotes[(size_t) i] == styleVotes[(size_t) bestStyle] && styleVotes[(size_t) i] > 0.0f
                && styleFirstSeen[(size_t) i] < styleFirstSeen[(size_t) bestStyle]))
            bestStyle = i;
    p.style = styleVotes[(size_t) bestStyle] >= 0.5f ? (Style) bestStyle : Style::Auto;
    p.clampAll();

    for (const auto& [section, acc] : perSection)
    {
        MoodOverride o;
        if (acc.touched[0]) o.valence = p.valence + acc.v;
        if (acc.touched[1]) o.energy = p.energy + acc.e;
        if (acc.touched[2]) o.tension = p.tension + acc.t;
        if (acc.touched[3]) o.darkness = p.darkness + acc.d;
        if (acc.touched[4]) o.sophistication = p.sophistication + acc.s;
        if (acc.cpb > 0.0f) o.chordsPerBar = acc.cpb;
        if (acc.colors != 0) o.colors = colorsFromBits (acc.colors | global.colors);
        if (! o.empty())
            p.sections[section] = o;
    }
    p.clampAll();
    return reading;
}

} // namespace so::song
