#include "MoodProfile.h"

#include "MiniJson.h"

#include <algorithm>
#include <cmath>

namespace so::song
{

namespace
{
    float clampf (float v, float lo, float hi) { return std::clamp (v, lo, hi); }

    void clampOverride (MoodOverride& o)
    {
        if (o.valence) *o.valence = clampf (*o.valence, -1.0f, 1.0f);
        if (o.energy) *o.energy = clampf (*o.energy, 0.0f, 1.0f);
        if (o.tension) *o.tension = clampf (*o.tension, 0.0f, 1.0f);
        if (o.darkness) *o.darkness = clampf (*o.darkness, 0.0f, 1.0f);
        if (o.sophistication) *o.sophistication = clampf (*o.sophistication, 0.0f, 1.0f);
        if (o.chordsPerBar) *o.chordsPerBar = *o.chordsPerBar <= 0.0f ? 0.0f : clampf (*o.chordsPerBar, 0.25f, 4.0f);
    }

    double q3 (float v) { return std::round ((double) v * 1000.0) / 1000.0; }

    json::Value colorsToJson (const std::vector<ColorFamily>& cs)
    {
        auto a = json::Value::makeArray();
        for (auto c : cs)
            a.array.push_back (json::Value::makeString (std::string (toString (c))));
        return a;
    }

    void dimsToJson (json::Value& o, const MoodOverride& m)
    {
        if (m.valence) o.set ("valence", json::Value::makeNumber (q3 (*m.valence)));
        if (m.energy) o.set ("energy", json::Value::makeNumber (q3 (*m.energy)));
        if (m.tension) o.set ("tension", json::Value::makeNumber (q3 (*m.tension)));
        if (m.darkness) o.set ("darkness", json::Value::makeNumber (q3 (*m.darkness)));
        if (m.sophistication) o.set ("sophistication", json::Value::makeNumber (q3 (*m.sophistication)));
        if (m.chordsPerBar) o.set ("chordsPerBar", json::Value::makeNumber (q3 (*m.chordsPerBar)));
        if (m.colors) o.set ("colors", colorsToJson (*m.colors));
    }

    bool readNumber (const json::Value& obj, std::string_view key, std::optional<float>& out, std::string& err)
    {
        const auto* v = obj.find (key);
        if (v == nullptr || v->type == json::Value::Type::Null)
            return true;
        if (! v->isNumber()) { err = "\"" + std::string (key) + "\" must be a number"; return false; }
        out = (float) v->number;
        return true;
    }

    bool readColors (const json::Value& obj, std::optional<std::vector<ColorFamily>>& out, std::string& err)
    {
        const auto* v = obj.find ("colors");
        if (v == nullptr || v->type == json::Value::Type::Null)
            return true;
        if (! v->isArray()) { err = "\"colors\" must be an array"; return false; }
        std::vector<ColorFamily> cs;
        for (const auto& item : v->array)
            if (item.isString())
                if (auto c = colorFamilyFromString (item.string))
                    if (std::find (cs.begin(), cs.end(), *c) == cs.end())
                        cs.push_back (*c);   // unknown names are ignored, not fatal
        out = std::move (cs);
        return true;
    }

    bool readOverride (const json::Value& obj, MoodOverride& m, std::string& err)
    {
        return readNumber (obj, "valence", m.valence, err)
            && readNumber (obj, "energy", m.energy, err)
            && readNumber (obj, "tension", m.tension, err)
            && readNumber (obj, "darkness", m.darkness, err)
            && readNumber (obj, "sophistication", m.sophistication, err)
            && readNumber (obj, "chordsPerBar", m.chordsPerBar, err)
            && readColors (obj, m.colors, err);
    }

    template <typename E, size_t N>
    std::string joinNames (std::string_view (*name) (E))
    {
        std::string s;
        for (size_t i = 0; i < N; ++i)
            s += (i ? ", " : "") + std::string (name ((E) (int) i));
        return s;
    }
}

MoodProfile MoodProfile::resolved (Section section) const
{
    MoodProfile r = *this;
    r.sections.clear();
    const auto it = sections.find (section);
    if (it == sections.end())
        return r;

    const auto& o = it->second;
    if (o.valence) r.valence = *o.valence;
    if (o.energy) r.energy = *o.energy;
    if (o.tension) r.tension = *o.tension;
    if (o.darkness) r.darkness = *o.darkness;
    if (o.sophistication) r.sophistication = *o.sophistication;
    if (o.chordsPerBar) r.chordsPerBar = *o.chordsPerBar;
    if (o.colors) r.colors = *o.colors;
    r.clampAll();
    return r;
}

void MoodProfile::clampAll()
{
    valence = clampf (valence, -1.0f, 1.0f);
    energy = clampf (energy, 0.0f, 1.0f);
    tension = clampf (tension, 0.0f, 1.0f);
    darkness = clampf (darkness, 0.0f, 1.0f);
    sophistication = clampf (sophistication, 0.0f, 1.0f);
    chordsPerBar = chordsPerBar <= 0.0f ? 0.0f : clampf (chordsPerBar, 0.25f, 4.0f);
    for (auto it = sections.begin(); it != sections.end();)
    {
        clampOverride (it->second);
        it = it->second.empty() ? sections.erase (it) : std::next (it);
    }
}

bool MoodProfile::prefers (ColorFamily c) const
{
    return std::find (colors.begin(), colors.end(), c) != colors.end();
}

std::string toJson (const MoodProfile& p)
{
    auto o = json::Value::makeObject();
    o.set ("schemaVersion", json::Value::makeNumber (1));
    MoodOverride dims;
    dims.valence = p.valence;
    dims.energy = p.energy;
    dims.tension = p.tension;
    dims.darkness = p.darkness;
    dims.sophistication = p.sophistication;
    dimsToJson (o, dims);
    o.set ("style", json::Value::makeString (std::string (toString (p.style))));
    o.set ("chordsPerBar", json::Value::makeNumber (q3 (p.chordsPerBar)));
    o.set ("colors", colorsToJson (p.colors));

    auto secs = json::Value::makeObject();
    for (const auto& [section, ov] : p.sections)
    {
        if (ov.empty())
            continue;
        auto so = json::Value::makeObject();
        dimsToJson (so, ov);
        secs.set (std::string (toString (section)), std::move (so));
    }
    o.set ("sections", std::move (secs));
    return json::write (o) + "\n";
}

std::optional<MoodProfile> moodFromJson (std::string_view text, std::string* error)
{
    std::string err;
    auto fail = [&] (std::string msg) -> std::optional<MoodProfile>
    {
        if (error) *error = std::move (msg);
        return std::nullopt;
    };

    const auto doc = json::parse (text);
    if (! doc) return fail ("Not valid JSON");
    if (! doc->isObject()) return fail ("The top level must be an object");

    MoodOverride dims;
    if (! readOverride (*doc, dims, err))
        return fail (err);

    MoodProfile p;
    if (dims.valence) p.valence = *dims.valence;
    if (dims.energy) p.energy = *dims.energy;
    if (dims.tension) p.tension = *dims.tension;
    if (dims.darkness) p.darkness = *dims.darkness;
    if (dims.sophistication) p.sophistication = *dims.sophistication;
    if (dims.chordsPerBar) p.chordsPerBar = *dims.chordsPerBar;
    if (dims.colors) p.colors = *dims.colors;

    if (const auto* s = doc->find ("style"); s != nullptr && s->type != json::Value::Type::Null)
    {
        if (! s->isString()) return fail ("\"style\" must be a string");
        p.style = styleFromString (s->string).value_or (Style::Auto);
    }

    if (const auto* secs = doc->find ("sections"); secs != nullptr && secs->type != json::Value::Type::Null)
    {
        if (! secs->isObject()) return fail ("\"sections\" must be an object");
        for (const auto& [name, val] : secs->object)
        {
            const auto section = sectionFromString (name);
            if (! section) continue;   // unknown section names are ignored
            if (! val.isObject()) return fail ("Section \"" + name + "\" must be an object");
            MoodOverride ov;
            if (! readOverride (val, ov, err))
                return fail ("Section \"" + name + "\": " + err);
            p.sections[*section] = ov;
        }
    }

    p.clampAll();
    return p;
}

const std::string_view kMoodProfileJsonSchema = R"SCHEMA({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "second-opinion/mood-profile/v1",
  "title": "MoodProfile",
  "description": "The mood of a song as numbers. All fields are optional; missing fields use the defaults shown.",
  "type": "object",
  "additionalProperties": false,
  "$defs": {
    "colors": {
      "type": "array",
      "items": { "enum": ["Diatonic", "Borrowed", "SecondaryDominants", "TritoneSubs", "ChromaticMediants", "Suspensions", "Extensions", "Modal", "Pedal"] },
      "uniqueItems": true
    },
    "override": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "valence": { "type": "number", "minimum": -1, "maximum": 1 },
        "energy": { "type": "number", "minimum": 0, "maximum": 1 },
        "tension": { "type": "number", "minimum": 0, "maximum": 1 },
        "darkness": { "type": "number", "minimum": 0, "maximum": 1 },
        "sophistication": { "type": "number", "minimum": 0, "maximum": 1 },
        "chordsPerBar": { "type": "number", "minimum": 0, "maximum": 4 },
        "colors": { "$ref": "#/$defs/colors" }
      }
    }
  },
  "properties": {
    "schemaVersion": { "const": 1 },
    "valence": { "type": "number", "minimum": -1, "maximum": 1, "default": 0, "description": "-1 sad .. +1 happy" },
    "energy": { "type": "number", "minimum": 0, "maximum": 1, "default": 0.5, "description": "0 calm .. 1 intense" },
    "tension": { "type": "number", "minimum": 0, "maximum": 1, "default": 0.3, "description": "0 settled .. 1 unresolved, dominant-heavy" },
    "darkness": { "type": "number", "minimum": 0, "maximum": 1, "default": 0.3, "description": "0 bright .. 1 dark (minor, Phrygian, borrowed chords)" },
    "sophistication": { "type": "number", "minimum": 0, "maximum": 1, "default": 0.4, "description": "0 plain triads .. 1 extended jazz and neo-soul colors" },
    "style": { "enum": ["Auto", "RnB", "NeoSoul", "HipHop", "Trap", "Drill", "Pop", "Gospel", "LoFi", "Cinematic", "Rock", "EDM", "Afrobeats", "Latin", "Jazz", "Country"], "default": "Auto" },
    "chordsPerBar": { "type": "number", "minimum": 0, "maximum": 4, "default": 0, "description": "Harmonic rhythm. 0 = style default; 0.5 = one chord every two bars; 2 = two chords per bar" },
    "colors": { "$ref": "#/$defs/colors" },
    "sections": {
      "type": "object",
      "additionalProperties": false,
      "description": "Per-section overrides. Only include fields that differ from the global values.",
      "properties": {
        "Intro": { "$ref": "#/$defs/override" },
        "Verse": { "$ref": "#/$defs/override" },
        "PreChorus": { "$ref": "#/$defs/override" },
        "Chorus": { "$ref": "#/$defs/override" },
        "PostChorus": { "$ref": "#/$defs/override" },
        "Bridge": { "$ref": "#/$defs/override" },
        "Breakdown": { "$ref": "#/$defs/override" },
        "Outro": { "$ref": "#/$defs/override" }
      }
    }
  }
})SCHEMA";

std::string moodSystemPrompt()
{
    std::string p;
    p += "You translate a music producer's description of a song's mood into numbers for a chord "
         "progression engine. Reply with ONE JSON object and nothing else: no prose, no Markdown.\n\n";
    p += "Fields (all optional; omit a field to keep its default):\n";
    p += "- valence: -1 (sad) to 1 (happy). Default 0.\n";
    p += "- energy: 0 (calm, sparse) to 1 (intense, driving). Default 0.5.\n";
    p += "- tension: 0 (settled, resolved) to 1 (unresolved, dominant-heavy, suspended). Default 0.3.\n";
    p += "- darkness: 0 (bright, major, Lydian) to 1 (dark, minor, Phrygian, borrowed chords). Default 0.3.\n";
    p += "- sophistication: 0 (plain triads) to 1 (7ths, 9ths, neo-soul and jazz colors). Default 0.4.\n";
    p += "- style: one of " + joinNames<Style, numStyles> (&toString) + ". Use Auto when no genre is implied.\n";
    p += "- chordsPerBar: harmonic rhythm, 0 for the style default, else 0.25 to 4 (0.5 = one chord every two bars).\n";
    p += "- colors: preferred technique families, any of " + joinNames<ColorFamily, numColorFamilies> (&toString) + ".\n";
    p += "- sections: per-section overrides keyed by " + joinNames<Section, numSections> (&toString)
       + ". Each holds only the fields that differ from the global values. A \"hook\" is the Chorus, a \"pre\" is the PreChorus.\n\n";
    p += "Rules:\n";
    p += "1. Words tied to a section (\"hopeful chorus\", \"a bridge that feels like sunrise\") go in that section's override, as absolute values.\n";
    p += "2. Respect negation and degree: \"not too dark\" lowers darkness a little; \"very dark\" pushes it near 1.\n";
    p += "3. Selected mood chips (" + joinNames<MoodChip, numMoodChips> (&toString) + ") apply to the whole song.\n";
    p += "4. Keep numbers to at most 2 decimals. Include \"schemaVersion\": 1.\n\n";
    p += "Example. Input: \"dark late-night R&B, hopeful chorus, bridge that feels like sunrise\"\n";
    p += "Output: {\"schemaVersion\":1,\"valence\":-0.35,\"energy\":0.35,\"tension\":0.4,\"darkness\":0.8,"
         "\"sophistication\":0.7,\"style\":\"RnB\",\"chordsPerBar\":0,\"colors\":[\"Extensions\"],"
         "\"sections\":{\"Chorus\":{\"valence\":0.2,\"darkness\":0.55},\"Bridge\":{\"valence\":0.5,\"darkness\":0.3,\"energy\":0.55}}}\n\n";
    p += "The reply must validate against this JSON Schema:\n";
    p += std::string (kMoodProfileJsonSchema);
    p += "\n";
    return p;
}

std::string moodUserPrompt (std::string_view description, const std::vector<MoodChip>& chips)
{
    std::string u = "Description: ";
    u += description.empty() ? std::string ("(none)") : std::string (description);
    u += "\nMood chips: ";
    if (chips.empty())
        u += "(none)";
    for (size_t i = 0; i < chips.size(); ++i)
        u += (i ? ", " : "") + std::string (toString (chips[i]));
    return u + "\n";
}

} // namespace so::song
