#include "PreviewSynth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace so::preview
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    bool laneIsBass (const std::string& name)
    {
        std::string lower (name);
        std::transform (lower.begin(), lower.end(), lower.begin(),
                        [] (unsigned char c) { return (char) std::tolower (c); });
        return lower.find ("bass") != std::string::npos;
    }

    float coefForTau (double tauSec, double sr) noexcept
    {
        return (float) std::exp (-1.0 / (std::max (tauSec, 1.0e-4) * sr));
    }

    inline float polyBlepSaw (float t, float dt) noexcept
    {
        float s = 2.0f * t - 1.0f;
        if (t < dt)
        {
            const float x = t / dt;
            s -= x + x - x * x - 1.0f;
        }
        else if (t > 1.0f - dt)
        {
            const float x = (t - 1.0f) / dt;
            s -= x * x + x + x + 1.0f;
        }
        return s;
    }

    inline float onePole (float x, float g, float& state) noexcept
    {
        const float v = (x - state) * g;
        const float y = v + state;
        state = y + v;
        return y;
    }

    constexpr float silentEnv = 1.0e-4f;
}

PreviewSnapshot makePreviewSnapshot (const std::vector<midi::MidiLane>& lanes, double bpm, double lengthBeats)
{
    PreviewSnapshot snap;
    snap.bpm = bpm > 0.0 ? bpm : 120.0;

    double lastEnd = 0.0;
    for (const auto& lane : lanes)
    {
        const bool bass = laneIsBass (lane.name);
        for (const auto& n : lane.notes)
        {
            if (! (n.lengthBeats > 0.0) || n.pitch < 0 || n.pitch > 127 || ! std::isfinite (n.startBeat))
                continue;
            PreviewNote p;
            p.startBeat = n.startBeat;
            p.endBeat = n.startBeat + n.lengthBeats;
            p.pitch = n.pitch;
            p.velocity = (float) std::clamp (n.velocity, 1, 127) / 127.0f;
            p.bass = bass;
            snap.notes.push_back (p);
            lastEnd = std::max (lastEnd, p.endBeat);
        }
    }

    std::stable_sort (snap.notes.begin(), snap.notes.end(),
                      [] (const PreviewNote& a, const PreviewNote& b) { return a.startBeat < b.startBeat; });
    snap.lengthBeats = lengthBeats > 0.0 ? lengthBeats : std::max (1.0, lastEnd);
    return snap;
}

PreviewSynth::PreviewSynth()
{
    prepare (48000.0);
}

void PreviewSynth::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;

    keysEnv = { (float) (1.0 / (0.002 * sampleRate)), coefForTau (0.9, sampleRate), 0.15f, coefForTau (0.12, sampleRate) };
    padEnv  = { (float) (1.0 / (0.35 * sampleRate)),  coefForTau (0.6, sampleRate), 0.8f,  coefForTau (0.35, sampleRate) };
    bassEnv = { (float) (1.0 / (0.004 * sampleRate)), coefForTau (0.3, sampleRate), 0.7f,  coefForTau (0.05, sampleRate) };
    fastReleaseCoef = coefForTau (0.004, sampleRate);
    keysModDecay = coefForTau (0.35, sampleRate);
    bassLpG = onePoleG (700.0f);
    padLpG = onePoleG (2200.0f);
    reset();
}

void PreviewSynth::reset() noexcept
{
    for (auto& v : voices)
        v = Voice {};
    lastSnapshot = nullptr;
    expectedBeat = -1.0e300;
    cursor = 0;
}

void PreviewSynth::releaseAll (bool fast) noexcept
{
    for (auto& v : voices)
    {
        if (v.stage == Stage::Off)
            continue;
        if (fast)
            v.releaseCoef = std::min (v.releaseCoef, fastReleaseCoef);
        v.stage = Stage::Release;
    }
}

int PreviewSynth::activeVoiceCount() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.stage != Stage::Off; });
}

float PreviewSynth::onePoleG (float cutoffHz) const noexcept
{
    const double fc = std::clamp ((double) cutoffHz, 20.0, sampleRate * 0.45);
    const double g = std::tan (pi * fc / sampleRate);
    return (float) (g / (1.0 + g));
}

const PreviewSynth::EnvParams& PreviewSynth::envFor (Kind k) const noexcept
{
    return k == Kind::Keys ? keysEnv : (k == Kind::Pad ? padEnv : bassEnv);
}

float PreviewSynth::sine (float phase) const noexcept
{
    // Branch-free parabolic sine with one refinement step (max error about 0.001), phase in cycles.
    // Valid for phase > -16, far beyond the +-0.4 cycle FM excursion used here.
    const float q = phase + 16.5f;
    const float x = q - (float) (int) q - 0.5f;          // [-0.5, 0.5)
    const float y = 8.0f * x - 16.0f * x * std::abs (x);
    return 0.225f * (y * std::abs (y) - y) + y;
}

void PreviewSynth::startNote (const PreviewNote& n) noexcept
{
    // Pick a free voice, else the oldest releasing one, else the oldest held one.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage == Stage::Off) { target = &v; break; }

    if (target == nullptr)
    {
        for (auto& v : voices)
            if (v.stage == Stage::Release && (target == nullptr || v.age < target->age))
                target = &v;
    }
    if (target == nullptr)
    {
        for (auto& v : voices)
            if (target == nullptr || v.age < target->age)
                target = &v;
    }

    Voice& v = *target;
    v = Voice {};
    v.kind = n.bass ? Kind::Bass : (sound == Sound::Pad ? Kind::Pad : Kind::Keys);
    v.pitch = n.pitch;
    v.endBeat = n.endBeat;
    v.age = ++ageCounter;
    v.stage = Stage::Attack;
    v.releaseCoef = envFor (v.kind).releaseCoef;

    const float vel = std::clamp (n.velocity, 0.0f, 1.0f);
    const float velCurve = 0.3f + 0.7f * vel;
    const double freq = 440.0 * std::pow (2.0, ((double) n.pitch - 69.0) / 12.0);
    const float inc = (float) (freq / sampleRate);

    switch (v.kind)
    {
        case Kind::Keys:
            v.gain = 0.22f * velCurve;
            v.inc[0] = inc;
            v.modInc = inc;                       // 1:1 ratio, classic tine-ish FM
            v.modIndex = 0.05f + 0.35f * vel;     // in cycles; velocity = brightness
            v.modDecay = keysModDecay;
            v.lpG = onePoleG (1200.0f + 5000.0f * vel);
            break;
        case Kind::Pad:
            v.gain = 0.07f * velCurve;
            v.inc[0] = inc * 0.99596f;            // -7 cents
            v.inc[1] = inc;
            v.inc[2] = inc * 1.00405f;            // +7 cents
            v.phase[1] = 0.33f;
            v.phase[2] = 0.66f;
            v.lpG = padLpG;
            break;
        case Kind::Bass:
            v.gain = 0.30f * velCurve;
            v.inc[0] = inc;
            v.lpG = bassLpG;
            break;
    }
}

void PreviewSynth::seek (const PreviewSnapshot& snap, double beat) noexcept
{
    releaseAll (true);
    const auto& notes = snap.notes;
    cursor = (std::size_t) (std::lower_bound (notes.begin(), notes.end(), beat,
                                              [] (const PreviewNote& n, double b) { return n.startBeat < b; })
                            - notes.begin());

    // Notes already sounding at the new position start now (audition from the middle of a chord).
    for (std::size_t i = 0; i < cursor; ++i)
        if (notes[i].endBeat > beat)
            startNote (notes[i]);
}

void PreviewSynth::render (const PreviewSnapshot* snapshot, float* left, float* right, int numSamples,
                           double startBeat, double beatsPerSample) noexcept
{
    if (numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    if (sound == Sound::Off)
        snapshot = nullptr;

    const bool jumped = std::abs (startBeat - expectedBeat) > 0.5 * std::abs (beatsPerSample) + 1.0e-9;
    if (snapshot != lastSnapshot || jumped)
    {
        if (snapshot != nullptr)
            seek (*snapshot, startBeat);
        else
            releaseAll (true);
        lastSnapshot = snapshot;
    }

    int done = 0;
    while (done < numSamples)
    {
        const double b = startBeat + (double) done * beatsPerSample;
        double next = std::numeric_limits<double>::infinity();

        if (snapshot != nullptr)
        {
            for (auto& v : voices)
                if (v.held() && b >= v.endBeat)
                    v.stage = Stage::Release;

            const auto& notes = snapshot->notes;
            while (cursor < notes.size() && notes[cursor].startBeat <= b)
            {
                if (notes[cursor].endBeat > b)
                    startNote (notes[cursor]);
                ++cursor;
            }

            if (cursor < notes.size())
                next = notes[cursor].startBeat;
            for (const auto& v : voices)
                if (v.held())
                    next = std::min (next, v.endBeat);
        }

        int len = numSamples - done;
        if (std::isfinite (next) && beatsPerSample > 0.0)
        {
            const double samplesToNext = std::ceil ((next - b) / beatsPerSample);
            len = (int) std::clamp (samplesToNext, 1.0, (double) len);
        }

        renderVoices (left + done, right + done, len);
        done += len;
    }

    expectedBeat = startBeat + (double) numSamples * beatsPerSample;
}

namespace
{
    template <typename Osc>
    inline void runVoice (float* mono, int numSamples, float& env, std::uint8_t& stage, float attackInc,
                          float decayCoef, float sustain, float releaseCoef, float gain, Osc&& osc) noexcept
    {
        // stage: 1 Attack, 2 Decay, 3 Sustain, 4 Release (matches PreviewSynth::Stage)
        for (int i = 0; i < numSamples; ++i)
        {
            if (stage == 1)
            {
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; stage = 2; }
            }
            else if (stage == 2)
                env = sustain + (env - sustain) * decayCoef;
            else if (stage == 4)
                env *= releaseCoef;

            mono[i] += osc() * env * gain;
        }
    }
}

void PreviewSynth::renderVoices (float* left, float* right, int numSamples) noexcept
{
    constexpr int chunk = 256;
    float mono[chunk];

    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = std::min (chunk, numSamples - start);
        std::fill (mono, mono + n, 0.0f);

        for (auto& v : voices)
        {
            if (v.stage == Stage::Off)
                continue;

            const EnvParams& ep = envFor (v.kind);
            auto stage = (std::uint8_t) v.stage;

            switch (v.kind)
            {
                case Kind::Keys:
                    runVoice (mono, n, v.env, stage, ep.attackInc, ep.decayCoef, ep.sustain, v.releaseCoef, v.gain,
                              [this, &v]() noexcept
                              {
                                  const float mod = v.modIndex * sine (v.modPhase);
                                  const float x = sine (v.phase[0] + mod);
                                  v.modIndex *= v.modDecay;
                                  v.modPhase += v.modInc;
                                  if (v.modPhase >= 1.0f) v.modPhase -= 1.0f;
                                  v.phase[0] += v.inc[0];
                                  if (v.phase[0] >= 1.0f) v.phase[0] -= 1.0f;
                                  return onePole (x, v.lpG, v.lp1);
                              });
                    break;
                case Kind::Pad:
                    runVoice (mono, n, v.env, stage, ep.attackInc, ep.decayCoef, ep.sustain, v.releaseCoef, v.gain,
                              [&v]() noexcept
                              {
                                  float x = 0.0f;
                                  for (int o = 0; o < 3; ++o)
                                  {
                                      x += polyBlepSaw (v.phase[o], v.inc[o]);
                                      v.phase[o] += v.inc[o];
                                      if (v.phase[o] >= 1.0f) v.phase[o] -= 1.0f;
                                  }
                                  return onePole (onePole (x, v.lpG, v.lp1), v.lpG, v.lp2);
                              });
                    break;
                case Kind::Bass:
                    runVoice (mono, n, v.env, stage, ep.attackInc, ep.decayCoef, ep.sustain, v.releaseCoef, v.gain,
                              [this, &v]() noexcept
                              {
                                  const float x = sine (v.phase[0]) + 0.35f * polyBlepSaw (v.phase[0], v.inc[0]);
                                  v.phase[0] += v.inc[0];
                                  if (v.phase[0] >= 1.0f) v.phase[0] -= 1.0f;
                                  return onePole (onePole (x, v.lpG, v.lp1), v.lpG, v.lp2);
                              });
                    break;
            }

            v.stage = (Stage) stage;
            if (v.stage == Stage::Release && v.env < silentEnv)
                v.stage = Stage::Off;
        }

        for (int i = 0; i < n; ++i)
        {
            left[start + i] += mono[i];
            right[start + i] += mono[i];
        }
    }
}

} // namespace so::preview
