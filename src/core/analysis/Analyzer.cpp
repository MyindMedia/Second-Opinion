#include "Analyzer.h"

#include "BeatAggregator.h"
#include "ChordDecoder.h"
#include "KeyEstimator.h"
#include "MelodyTracker.h"
#include "Resampler.h"

#include "../theory/ChordNamer.h"

#include <algorithm>
#include <cmath>

namespace so::analysis
{

namespace
{
    constexpr float kTargetPeak = 0.5f;
    constexpr float kMaxGain = 100.0f;   // +40 dB at most, so pure noise is not blown up

    ChordDecoderOptions decoderOptionsFor (const AnalyzerOptions& o, const BeatGrid& g)
    {
        ChordDecoderOptions d;
        d.beatsPerBar = g.beatsPerBar;
        if (o.source == SourceHint::Vocal)
        {
            // A lone voice implies harmony: lean on the key, prefer triads and slower changes.
            d.keyWeight = 0.4f;
            d.complexityWeight = 0.1f;
            d.changeCostDownbeat = 4.0f;
            d.changeCostMidBar = 6.0f;
            d.changeCostWeak = 10.0f;
        }
        else if (o.source == SourceHint::Loop)
        {
            d.changeCostWeak = 9.0f;
        }
        return d;
    }
}

AnalysisResult analyze (const AudioClip& clip, const BeatGrid& grid, const AnalyzerOptions& options,
                        ProgressFn progress, const std::atomic<bool>* cancel, AnalysisStatus* status)
{
    AnalysisResult res;
    res.grid = grid;

    AnalysisStatus local;
    AnalysisStatus& st = status != nullptr ? *status : local;
    st = {};

    auto cancelled = [&]
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
        {
            st.cancelled = true;
            return true;
        }
        return false;
    };
    auto report = [&] (AnalysisStage s, float pct)
    {
        if (progress)
            progress ((int) s, std::clamp (pct, 0.0f, 1.0f));
    };
    auto stageProgress = [&] (AnalysisStage s, float from, float to)
    {
        return [&report, s, from, to] (float p) { report (s, from + (to - from) * p); };
    };

    if (! (grid.bpm > 0.0) || ! std::isfinite (grid.bpm) || grid.beatsPerBar < 1 || ! (clip.sampleRate > 0.0))
    {
        st.message = "The tempo or sample rate looks wrong, so set the BPM and try again.";
        return res;
    }

    // ---- Stage 0: Preparing -------------------------------------------------------------------
    report (AnalysisStage::Preparing, 0.0f);
    if (cancelled())
        return res;

    const int bpb = grid.beatsPerBar;
    const int maxBeats = std::max (1, options.maxBars) * bpb;

    // Only resample what the grid can use (from the start up to maxBars after the downbeat).
    std::vector<float> input = clip.samples;
    const double endSec = grid.beatToSec (maxBeats);
    if (endSec > 0.0)
    {
        const auto keep = (size_t) std::ceil (endSec * clip.sampleRate) + 1;
        if (input.size() > keep)
            input.resize (keep);
    }

    std::vector<float> audio = resample (input, clip.sampleRate, kAnalysisRate, cancel);
    if (cancelled())
        return res;
    report (AnalysisStage::Preparing, 0.8f);

    float peak = 0.0f;
    for (float s : audio)
        peak = std::max (peak, std::abs (s));
    if (peak > 1e-6f)
    {
        const float gain = std::min (kTargetPeak / peak, kMaxGain);
        for (auto& s : audio)
            s *= gain;
    }

    const double durationSec = (double) audio.size() / kAnalysisRate;
    // A trailing partial beat counts only when it covers at least half a beat (release tails).
    const double audioBeats = grid.secToBeat (durationSec);
    if (audioBeats <= 0.0)
    {
        st.message = "The capture is too short to read, so record at least one beat.";
        report (AnalysisStage::Preparing, 1.0f);
        return res;
    }

    const int soundingBeats = std::min (maxBeats, std::max (1, (int) std::ceil (audioBeats - 0.5)));
    res.bars = (soundingBeats + bpb - 1) / bpb;
    const int numBeats = res.bars * bpb;
    report (AnalysisStage::Preparing, 1.0f);

    // ---- Stage 1: Transcribing notes ----------------------------------------------------------
    report (AnalysisStage::TranscribingNotes, 0.0f);
    if (cancelled())
        return res;

    std::vector<NoteEvent> melodySource;
    if (options.notes.has_value())
    {
        res.notes = *options.notes;
        melodySource = skyline (res.notes);
    }
    else
    {
        res.notes = MelodyTracker().track (audio, kAnalysisRate, cancel,
                                           stageProgress (AnalysisStage::TranscribingNotes, 0.0f, 0.95f));
        if (cancelled())
            return res;
        melodySource = res.notes;
    }
    res.melody = toMelodyNotes (melodySource, grid, numBeats);
    report (AnalysisStage::TranscribingNotes, 1.0f);

    // ---- Stage 2: Reading harmony -------------------------------------------------------------
    report (AnalysisStage::ReadingHarmony, 0.0f);
    if (cancelled())
        return res;

    const bool vocal = options.source == SourceHint::Vocal;
    const std::vector<NoteEvent>* chromaNotes = nullptr;
    float noteWeight = 0.0f;
    if (options.notes.has_value() && ! res.notes.empty())
    {
        chromaNotes = &res.notes;          // polyphonic transcription: a strong second opinion
        noteWeight = vocal ? 0.7f : 0.5f;
    }
    else if (vocal && ! res.notes.empty())
    {
        chromaNotes = &res.notes;          // the tracked melody is the harmony's only evidence
        noteWeight = 0.6f;
    }

    const ChromaExtractor extractor (ChromaSettings::forQuality (options.quality));
    const auto frames = extractor.process (audio, kAnalysisRate, chromaNotes, noteWeight, cancel,
                                           stageProgress (AnalysisStage::ReadingHarmony, 0.0f, 0.7f));
    if (cancelled())
        return res;

    const auto beats = aggregateBeats (frames, grid, numBeats);
    report (AnalysisStage::ReadingHarmony, 0.75f);

    const bool anySound = std::any_of (beats.begin(), beats.end(), [] (const BeatFeatures& b) { return ! b.silent; });
    if (! anySound)
    {
        st.message = vocal ? "Couldn't find a steady pitch, so try soloing the vocal."
                           : "Couldn't hear anything in the capture, so check the input level.";
        if (options.keyOverride)
        {
            res.key = *options.keyOverride;
            res.keyConfidence = 1.0f;
        }
        report (AnalysisStage::ReadingHarmony, 1.0f);
        return res;
    }

    auto decOpts = decoderOptionsFor (options, grid);
    const ChordDecoder decoder;

    if (options.keyOverride)
    {
        res.key = *options.keyOverride;
        res.keyConfidence = 1.0f;
        decOpts.key = res.key;
        res.segments = decoder.decode (beats, decOpts, cancel);
    }
    else
    {
        // Pass 1: key from chroma alone, chords with that key as a mild prior.
        const auto k0 = KeyEstimator::estimate (KeyEstimator::histogram (beats));
        decOpts.key = k0.key;
        res.segments = decoder.decode (beats, decOpts, cancel);
        if (cancelled())
            return res;
        report (AnalysisStage::ReadingHarmony, 0.85f);

        // Pass 2: refine the key with the decoded chords and their tonal centre (first and last
        // chord, cadences, bass); re-decode only if it moved.
        const auto k1 = KeyEstimator::estimateWithChords (beats, res.segments);
        res.key = k1.key;
        res.keyConfidence = k1.confidence;
        if (! (k1.key.tonic.pc() == k0.key.tonic.pc() && k1.key.mode == k0.key.mode))
        {
            decOpts.key = k1.key;
            res.segments = decoder.decode (beats, decOpts, cancel);
        }
    }
    if (cancelled())
        return res;

    // Spell every chord (and its alternatives) in the final key.
    for (auto& seg : res.segments)
    {
        seg.chord = theory::ChordNamer::respell (seg.chord, theory::NamingOptions::Spelling::Auto, &res.key);
        for (auto& alt : seg.alternatives)
            alt.chord = theory::ChordNamer::respell (alt.chord, theory::NamingOptions::Spelling::Auto, &res.key);
    }

    if (vocal && res.notes.empty())
        st.message = "Couldn't find a steady pitch, so try soloing the vocal.";

    report (AnalysisStage::ReadingHarmony, 1.0f);
    return res;
}

} // namespace so::analysis
