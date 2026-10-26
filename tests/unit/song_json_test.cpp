#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/song/MiniJson.h"
#include "core/song/MoodInterpreter.h"
#include "core/song/MoodProfile.h"

using namespace so::song;
using Catch::Approx;

namespace
{
    void requireClose (const MoodProfile& a, const MoodProfile& b)
    {
        CHECK (a.valence == Approx (b.valence).margin (0.001));
        CHECK (a.energy == Approx (b.energy).margin (0.001));
        CHECK (a.tension == Approx (b.tension).margin (0.001));
        CHECK (a.darkness == Approx (b.darkness).margin (0.001));
        CHECK (a.sophistication == Approx (b.sophistication).margin (0.001));
        CHECK (a.chordsPerBar == Approx (b.chordsPerBar).margin (0.001));
        CHECK (a.style == b.style);
        CHECK (a.colors == b.colors);
        REQUIRE (a.sections.size() == b.sections.size());
        for (const auto& [s, o] : a.sections)
        {
            REQUIRE (b.sections.count (s));
            const auto& p = b.sections.at (s);
            CHECK (o.valence.has_value() == p.valence.has_value());
            if (o.valence && p.valence) CHECK (*o.valence == Approx (*p.valence).margin (0.001));
            CHECK (o.darkness.has_value() == p.darkness.has_value());
            CHECK (o.colors == p.colors);
        }
    }
}

TEST_CASE ("MoodProfile JSON round trip", "[song][json]")
{
    MoodProfile p;
    p.valence = -0.35f;
    p.energy = 0.25f;
    p.tension = 0.6f;
    p.darkness = 0.875f;
    p.sophistication = 0.7f;
    p.style = Style::NeoSoul;
    p.chordsPerBar = 2.0f;
    p.colors = { ColorFamily::Extensions, ColorFamily::Borrowed };
    MoodOverride chorus;
    chorus.valence = 0.4f;
    chorus.darkness = 0.5f;
    chorus.colors = std::vector<ColorFamily> { ColorFamily::Suspensions };
    p.sections[Section::Chorus] = chorus;
    MoodOverride bridge;
    bridge.energy = 0.8f;
    bridge.chordsPerBar = 0.5f;
    p.sections[Section::Bridge] = bridge;

    const auto json = toJson (p);
    const auto back = moodFromJson (json);
    REQUIRE (back.has_value());
    requireClose (*back, p);
    CHECK (toJson (*back) == json);

    // Interpreter output also round trips.
    const auto read = MoodInterpreter::interpret ("dark late-night R&B, hopeful chorus, bridge that feels like sunrise");
    const auto back2 = moodFromJson (toJson (read));
    REQUIRE (back2.has_value());
    requireClose (*back2, read);
}

TEST_CASE ("MoodProfile JSON tolerates AI output", "[song][json]")
{
    const auto fenced = moodFromJson ("Here you go:\n```json\n{\"valence\": 0.5, \"style\": \"R&B\", \"sections\": {\"hook\": {\"energy\": 0.9}}}\n```\n");
    REQUIRE (fenced.has_value());
    CHECK (fenced->valence == Approx (0.5f));
    CHECK (fenced->style == Style::RnB);
    REQUIRE (fenced->sections.count (Section::Chorus));
    CHECK (*fenced->sections.at (Section::Chorus).energy == Approx (0.9f));
    CHECK (fenced->energy == Approx (0.5f));   // default kept

    const auto clamped = moodFromJson (R"({"valence": 7, "darkness": -2, "chordsPerBar": 9, "colors": ["Modal", "Nope", "Modal"], "extra": [1,2]})");
    REQUIRE (clamped.has_value());
    CHECK (clamped->valence == Approx (1.0f));
    CHECK (clamped->darkness == Approx (0.0f));
    CHECK (clamped->chordsPerBar == Approx (4.0f));
    CHECK (clamped->colors == std::vector<ColorFamily> { ColorFamily::Modal });

    const auto unknownStyle = moodFromJson (R"({"style": "polka"})");
    REQUIRE (unknownStyle.has_value());
    CHECK (unknownStyle->style == Style::Auto);

    std::string err;
    CHECK_FALSE (moodFromJson ("{\"valence\": \"high\"}", &err).has_value());
    CHECK (err.find ("valence") != std::string::npos);
    CHECK_FALSE (moodFromJson ("{not json", &err).has_value());
    CHECK_FALSE (moodFromJson ("[1, 2]", &err).has_value());
    CHECK_FALSE (moodFromJson (R"({"sections": {"Chorus": 3}})", &err).has_value());
}

TEST_CASE ("Schema and AI prompt", "[song][json]")
{
    const auto schema = json::parse (kMoodProfileJsonSchema);
    REQUIRE (schema.has_value());
    REQUIRE (schema->find ("properties") != nullptr);
    for (const char* key : { "valence", "energy", "tension", "darkness", "sophistication", "style", "chordsPerBar", "colors", "sections" })
        CHECK (schema->find ("properties")->find (key) != nullptr);

    const auto prompt = moodSystemPrompt();
    for (int i = 0; i < numStyles; ++i)
        CHECK (prompt.find (std::string (toString ((Style) i))) != std::string::npos);
    for (int i = 0; i < numMoodChips; ++i)
        CHECK (prompt.find (std::string (toString ((MoodChip) i))) != std::string::npos);
    CHECK (prompt.find ("second-opinion/mood-profile/v1") != std::string::npos);
    CHECK (prompt.find ("\xE2\x80\x94") == std::string::npos);   // no em dash

    // The example inside the prompt is itself valid input.
    const auto start = prompt.find ("Output: ") + 8;
    const auto end = prompt.find ('\n', start);
    const auto example = moodFromJson (prompt.substr (start, end - start));
    REQUIRE (example.has_value());
    CHECK (example->style == Style::RnB);

    const auto user = moodUserPrompt ("dark R&B", { MoodChip::Dark, MoodChip::Soulful });
    CHECK (user.find ("dark R&B") != std::string::npos);
    CHECK (user.find ("Dark, Soulful") != std::string::npos);
}

TEST_CASE ("MiniJson basics", "[song][json]")
{
    const auto v = json::parse (R"({"a": [1, 2.5, -3e2, true, false, null], "b": "x\"yé"})");
    REQUIRE (v.has_value());
    REQUIRE (v->find ("a")->array.size() == 6);
    CHECK (v->find ("a")->array[2].number == Approx (-300.0));
    CHECK (v->find ("b")->string == "x\"y\xC3\xA9");
    CHECK (json::parse (json::write (*v)).has_value());
    CHECK_FALSE (json::parse ("{\"a\": 1,}").has_value());
    CHECK_FALSE (json::parse ("1 2").has_value());
}

#include "core/song/LocaleFreeNumber.h"

#include <locale>

namespace
{
    struct CommaDecimal : std::numpunct<char>
    {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
}

TEST_CASE ("Number text conversions ignore a comma-decimal locale", "[song][json]")
{
    const auto previous = std::locale::global (std::locale (std::locale::classic(), new CommaDecimal));

    CHECK (so::text::parseDouble ("0.5").value_or (-1.0) == 0.5);
    CHECK (so::text::parseDouble ("-1.25e2").value_or (0.0) == -125.0);
    CHECK_FALSE (so::text::parseDouble ("0,5").has_value());
    CHECK_FALSE (so::text::parseDouble ("").has_value());
    CHECK_FALSE (so::text::parseDouble ("1.5x").has_value());
    CHECK (so::text::formatFixed (1234.5, 3) == "1234.500");
    CHECK (so::text::formatRoundTrip (0.1) == "0.10000000000000001");

    std::locale::global (previous);
}
