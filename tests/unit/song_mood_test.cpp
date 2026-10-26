#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/song/MoodInterpreter.h"

#include <cmath>

using namespace so::song;
using Catch::Approx;

namespace
{
    MoodProfile mood (std::string_view text, std::vector<MoodChip> chips = {})
    {
        return MoodInterpreter::interpret (text, chips);
    }

    MoodProfile sec (const MoodProfile& p, Section s) { return p.resolved (s); }

    bool hasColor (const MoodProfile& p, ColorFamily c) { return p.prefers (c); }
}

TEST_CASE ("Lexicon is large enough", "[song][mood]")
{
    CHECK (MoodInterpreter::lexiconSize() >= 300);
}

TEST_CASE ("Empty description gives the neutral profile", "[song][mood]")
{
    CHECK (mood ("") == MoodProfile {});
    CHECK (mood ("   the and of   ") == MoodProfile {});
}

TEST_CASE ("Product owner's example phrases", "[song][mood]")
{
    SECTION ("dark late-night R&B, hopeful chorus, bridge that feels like sunrise")
    {
        const auto p = mood ("dark late-night R&B, hopeful chorus, bridge that feels like sunrise");
        CHECK (p.style == Style::RnB);
        CHECK (p.darkness > 0.7f);
        CHECK (p.valence < 0.0f);
        CHECK (p.sophistication > 0.6f);
        REQUIRE (p.sections.count (Section::Chorus));
        REQUIRE (p.sections.count (Section::Bridge));
        CHECK (sec (p, Section::Chorus).valence > p.valence + 0.3f);
        CHECK (sec (p, Section::Chorus).darkness < p.darkness);
        CHECK (sec (p, Section::Bridge).valence > p.valence + 0.3f);
        CHECK (sec (p, Section::Bridge).darkness < sec (p, Section::Chorus).darkness);
        CHECK_FALSE (p.sections.count (Section::Verse));
    }
    SECTION ("aggressive drill, minor, cold")
    {
        const auto p = mood ("aggressive drill, minor, cold");
        CHECK (p.style == Style::Drill);
        CHECK (p.darkness >= 0.9f);
        CHECK (p.energy > 0.7f);
        CHECK (p.valence < -0.5f);
        CHECK (p.sophistication < 0.3f);
    }
    SECTION ("uplifting gospel choir chords")
    {
        const auto p = mood ("uplifting gospel choir chords");
        CHECK (p.style == Style::Gospel);
        CHECK (p.valence > 0.5f);
        CHECK (p.sophistication > 0.7f);
        CHECK (p.darkness < 0.2f);
    }
    SECTION ("lofi chill rainy day")
    {
        const auto p = mood ("lofi chill rainy day");
        CHECK (p.style == Style::LoFi);
        CHECK (p.energy < 0.2f);
        CHECK (p.sophistication > 0.6f);
    }
}

TEST_CASE ("Negation and intensity", "[song][mood]")
{
    const float base = MoodProfile {}.darkness;
    CHECK (mood ("not too dark").darkness < base);
    CHECK (mood ("not too dark").valence > 0.0f);
    CHECK (mood ("dark").darkness > base);

    const float slight = mood ("slightly dark").darkness;
    const float plain = mood ("dark").darkness;
    const float very = mood ("very dark").darkness;
    CHECK (slight < plain);
    CHECK (plain < very);
    CHECK (slight > base);

    CHECK (mood ("super happy").valence > mood ("happy").valence);
    CHECK (mood ("a little sad").valence > mood ("sad").valence);
    CHECK (mood ("kinda chill").energy > mood ("chill").energy);
    CHECK (mood ("not aggressive at all").energy < 0.5f);
    CHECK (mood ("no trap").style == Style::Auto);

    const auto r = MoodInterpreter::read ("it should not be busy");
    REQUIRE (r.terms.size() == 1);
    CHECK (r.terms[0].negated);
    CHECK (r.profile.chordsPerBar == 0.0f);
}

TEST_CASE ("Section binding", "[song][mood]")
{
    SECTION ("comma separated clauses")
    {
        const auto p = mood ("chill verse, the chorus goes hard");
        CHECK (p.energy == Approx (0.5f));
        CHECK (sec (p, Section::Verse).energy < 0.3f);
        CHECK (sec (p, Section::Chorus).energy > 0.8f);
    }
    SECTION ("nearest section word inside one clause")
    {
        const auto p = mood ("dark verse and hopeful chorus");
        CHECK (sec (p, Section::Verse).darkness > sec (p, Section::Chorus).darkness);
        CHECK (sec (p, Section::Chorus).valence > 0.3f);
        CHECK (sec (p, Section::Verse).valence < 0.0f);
    }
    SECTION ("pre-chorus, hook and intro words")
    {
        const auto p = mood ("sad verse, euphoric hook, tense pre-chorus, mysterious spacey intro");
        CHECK (sec (p, Section::Verse).valence < 0.0f);
        CHECK (sec (p, Section::Chorus).valence > 0.4f);
        CHECK (sec (p, Section::PreChorus).tension > 0.6f);
        CHECK (sec (p, Section::Intro).tension > 0.5f);
        CHECK (p.valence == Approx (0.0f));
    }
    SECTION ("bridge and outro")
    {
        const auto p = mood ("a bridge that feels like sunrise and a soft outro");
        CHECK (sec (p, Section::Bridge).valence > 0.3f);
        CHECK (sec (p, Section::Outro).energy < 0.3f);
    }
    SECTION ("genre words describe the whole song even next to a section")
    {
        const auto p = mood ("euphoric EDM drop");
        CHECK (p.style == Style::EDM);
        CHECK (sec (p, Section::Chorus).valence > p.valence);
    }
    SECTION ("but splits clauses")
    {
        const auto p = mood ("melancholic but hopeful");
        CHECK (std::abs (p.valence) < 0.2f);
        CHECK (p.sections.empty());
    }
}

TEST_CASE ("Styles, slang and synonyms", "[song][mood]")
{
    CHECK (mood ("happy summer pop").style == Style::Pop);
    CHECK (mood ("happy summer pop").valence > 0.7f);

    const auto ballad = mood ("sad piano ballad");
    CHECK (ballad.valence < -0.4f);
    CHECK (ballad.energy < 0.3f);

    const auto neo = mood ("smooth neo-soul with lush extensions");
    CHECK (neo.style == Style::NeoSoul);
    CHECK (neo.sophistication > 0.9f);
    CHECK (hasColor (neo, ColorFamily::Extensions));

    const auto trap = mood ("hard trap beat, 808s");
    CHECK (trap.style == Style::Trap);
    CHECK (trap.energy > 0.7f);
    CHECK (trap.sophistication < 0.2f);

    const auto cin = mood ("cinematic, epic, triumphant");
    CHECK (cin.style == Style::Cinematic);
    CHECK (cin.energy > 0.8f);
    CHECK (cin.valence > 0.4f);

    const auto vibey = mood ("vibey late night");
    CHECK (vibey.sophistication > 0.5f);
    CHECK (vibey.darkness > 0.4f);

    const auto jazzy = mood ("jazzy tritone subs");
    CHECK (jazzy.sophistication > 0.8f);
    CHECK (hasColor (jazzy, ColorFamily::TritoneSubs));

    CHECK (mood ("afrobeats, warm and bouncy").style == Style::Afrobeats);
    CHECK (mood ("afrobeats, warm and bouncy").valence > 0.5f);

    const auto boom = mood ("gritty boom bap");
    CHECK (boom.style == Style::HipHop);
    CHECK (boom.darkness > 0.5f);

    const auto jam = mood ("romantic slow jam");
    CHECK (jam.style == Style::RnB);
    CHECK (jam.sophistication > 0.6f);
    CHECK (jam.energy < 0.3f);

    CHECK (mood ("R & B").style == Style::RnB);
    CHECK (mood ("rnb").style == Style::RnB);
    CHECK (mood ("lo-fi").style == Style::LoFi);

    const auto stems = MoodInterpreter::read ("darker and moodier");
    CHECK (stems.terms.size() == 2);
    CHECK (stems.profile.darkness > 0.8f);

    const auto spacey = mood ("it should be very spacey and kinda dreamy");
    CHECK (hasColor (spacey, ColorFamily::Suspensions));
    CHECK (spacey.sophistication > 0.7f);

    CHECK (mood ("fast chord changes").chordsPerBar == Approx (2.0f));
    CHECK (mood ("sparse and minimal").chordsPerBar == Approx (0.5f));
}

TEST_CASE ("Mood chips", "[song][mood]")
{
    const auto dark = mood ("", { MoodChip::Dark, MoodChip::Gritty });
    CHECK (dark.darkness > 0.7f);
    CHECK (dark.sophistication < 0.4f);

    const auto pop = mood ("pop", { MoodChip::Hopeful });
    CHECK (pop.style == Style::Pop);
    CHECK (pop.valence > 0.5f);

    // Every chip is in the lexicon and moves the profile.
    for (int i = 0; i < numMoodChips; ++i)
    {
        const auto r = MoodInterpreter::read ("", { (MoodChip) i });
        REQUIRE (r.terms.size() == 1);
        CHECK (r.terms[0].fromChip);
        CHECK_FALSE (r.profile == MoodProfile {});
    }
}

TEST_CASE ("Mood reading is deterministic", "[song][mood]")
{
    const char* text = "dark verse and hopeful chorus, gospel bridge, very spacey intro";
    CHECK (toJson (mood (text)) == toJson (mood (text)));
}
