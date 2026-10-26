#pragma once

// Output types of the reharmonization engine (PRD 5.3, 5.4, 7.5).

#include "../midi/MidiWriter.h"
#include "../midi/SimpleVoicer.h"
#include "../theory/Chord.h"
#include "../theory/RomanNumeral.h"
#include "Settings.h"

#include <string>
#include <string_view>
#include <vector>

namespace so::reharm
{

/** Concrete techniques. Each belongs to exactly one Family. */
enum class Technique
{
    Original,            // unchanged chord
    Extension,           // same root, richer or simpler quality (7/9/6/sus)
    DiatonicSub,         // relative / mediant / common-tone diatonic substitute
    ModalInterchange,    // borrowed from the parallel key
    Backdoor,            // bVII7 resolving to I
    SecondaryDominant,   // V7/x, inserted or replacing
    IiVInsertion,        // ii-V into the next chord
    TritoneSub,          // subV7 replacing a dominant
    ChromaticApproach,   // subV7 inserted before the next chord
    ChromaticMediant,
    NeoRiemannianP,
    NeoRiemannianL,
    NeoRiemannianR,
};

Family familyOf (Technique t) noexcept;

/** Plain name, e.g. "tritone substitution". */
std::string_view toString (Technique t) noexcept;

/** Structured explanation (FR-EX-01/02). Text is built from these fields by templates, no LLM. */
struct Explanation
{
    theory::HarmonicFunction function { theory::HarmonicFunction::Other };
    std::string roman;                        // "subV7", "bVImaj7"
    Technique technique { Technique::Original };
    std::string techniqueTerm;                // "tritone substitution, subV7"
    std::string melodyRelation;               // "The melody C is the 9th of Bbmaj9."
    std::vector<std::string> voiceLeadingNotes;
    std::string simple;                       // at most 3 short sentences, plain language first
    std::string detailed;                     // adds voice-leading notes
};

enum class Depth { Simple, Detailed };

struct SuggestionChord
{
    int segmentId { 0 };
    double startBeat { 0.0 };
    double beats { 4.0 };
    theory::Chord chord;
    std::string symbol;                       // ChordNamer, Pop style
    bool changed { false };
    std::string roman;
    theory::HarmonicFunction function { theory::HarmonicFunction::Other };
    Technique technique { Technique::Original };
    int tension { 0 };                        // chordTension, 0..100
    Explanation explanation;
};

struct Suggestion
{
    std::string id;
    std::string title;                        // "Borrowed bVI lift"
    std::vector<Technique> techniques;
    unsigned families { 0 };                  // bitmask of Family used
    int tension { 0 };                        // 0..100, duration-weighted chord tension
    double movement { 0.0 };                  // average semitones of upper-voice movement per change
    double score { 0.0 };                     // ranker score (higher is better)
    std::string summary;
    std::vector<SuggestionChord> chords;
    midi::VoicedLanes lanes;                  // chords / bass / top line, ready for MidiWriter

    std::vector<midi::MidiLane> laneList() const { return { lanes.chords, lanes.bass, lanes.top }; }
};

} // namespace so::reharm
