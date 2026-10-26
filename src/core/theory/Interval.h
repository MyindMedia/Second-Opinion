#pragma once

#include <string>

namespace so::theory
{

/** A chord-tone interval above a root: semitones plus scale degree (1, 3, 5, 7, 9, 2, 4, 6...).
    The degree decides the letter name, so the same semitone count can spell differently
    (♯9 vs ♭3). */
struct Interval
{
    int semitones { 0 };
    int degree { 1 };

    /** Letter steps above the root implied by the degree (degree 9 → 1 step, like a 2nd). */
    int letterSteps() const noexcept { return (degree - 1) % 7; }

    /** Short musician-facing label: "R", "3", "♭3", "5", "♭5", "♯5", "6", "♭7", "7", "𝄫7", "9", "2", "4". */
    std::string label() const;

    bool operator== (const Interval&) const = default;
};

} // namespace so::theory
