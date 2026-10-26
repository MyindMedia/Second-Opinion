#pragma once

#include "Chord.h"
#include "Key.h"

#include <string>

namespace so::theory
{

/** Harmonic function, shown as the colored tick in the RomanRow and the chip in the WhyPanel. */
enum class HarmonicFunction { Tonic, Predominant, Dominant, Other };

std::string_view toString (HarmonicFunction f);

/** Roman-numeral reading of a chord in a key (FR-AN-02 AC3, FR-EX-01). */
struct RomanNumeral
{
    std::string numeral;          // e.g. "ii7", "V7/V", "♭VImaj7", "subV7", "viiø7"
    HarmonicFunction function { HarmonicFunction::Other };
    bool diatonic { false };      // every chord tone is in the key
    bool borrowed { false };      // not diatonic, but every tone is in the parallel key (modal interchange)
    bool secondary { false };     // secondary dominant (V/x)
    bool tritoneSub { false };    // dominant a half step above its target (subV7)

    /** Plain-language technique label for explanations ("Secondary dominant", "Modal interchange"...). */
    std::string technique() const;

    static RomanNumeral analyze (const Chord& chord, const Key& key,
                                 AccidentalStyle style = AccidentalStyle::Unicode);
};

} // namespace so::theory
