// so_analyze_cli: runs the Second Opinion listening engine on a WAV file and prints JSON
// (grid, key, chord segments). Used by tools/eval/eval_chords.py (P2-05). No JUCE.
//
//   so_analyze_cli file.wav [--host-bpm 92] [--bpm 92 --downbeat 0.12] [--quality balanced]
//                           [--source auto|vocal|keys|guitar|loop] [--basic-pitch nmp.onnx]
//
// Without --bpm the grid comes from chooseFileGrid (detected tempo and downbeat, with the host
// tempo winning when the audio agrees), then trimTrailingTail, exactly as for a dropped file.

#include "WavReader.h"

#include "core/analysis/Analyzer.h"
#include "core/analysis/TempoEstimator.h"
#include "core/theory/ChordNamer.h"

#if SO_HAS_ONNX
 #include "core/analysis/BasicPitchRunner.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using namespace so::analysis;

namespace
{
    const char* qualityName (so::theory::ChordQuality q)
    {
        static const char* names[] { "maj", "min", "dim", "aug", "sus2", "sus4", "6", "min6", "7", "maj7",
                                     "min7", "hdim7", "dim7", "add9", "9", "maj9", "min9", "7sus4" };
        const auto i = (int) q;
        return i >= 0 && i < (int) std::size (names) ? names[i] : "?";
    }

    std::string jsonEscape (const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '"' || c == '\\')
                o += '\\';
            if ((unsigned char) c < 0x20)
                continue;
            o += c;
        }
        return o;
    }

    std::string num (double v)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.4f", v);
        return b;
    }

    std::string chordJson (const so::theory::Chord& c)
    {
        const so::theory::NamingOptions ascii { so::theory::NamingOptions::Style::Pop, true,
                                                so::theory::NamingOptions::Spelling::Auto,
                                                so::theory::AccidentalStyle::Ascii };
        std::ostringstream o;
        o << "\"symbol\": \"" << jsonEscape (so::theory::ChordNamer::name (c, ascii)) << "\", \"root\": "
          << c.root.pc() << ", \"quality\": \"" << qualityName (c.quality) << "\", \"bass\": " << c.bassPc();
        return o.str();
    }

    int usage()
    {
        std::cerr << "usage: so_analyze_cli file.wav [--host-bpm N] [--bpm N] [--downbeat S] [--quality fast|balanced|accurate]\n"
                     "                      [--source auto|vocal|keys|guitar|loop] [--basic-pitch nmp.onnx] [--no-trim]\n";
        return 2;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return usage();

    std::string path, basicPitch;
    std::optional<double> hostBpm, bpm;
    double downbeat = 0.0;
    bool trim = true;
    AnalyzerOptions opts;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--host-bpm") hostBpm = std::atof (next().c_str());
        else if (a == "--bpm") bpm = std::atof (next().c_str());
        else if (a == "--downbeat") downbeat = std::atof (next().c_str());
        else if (a == "--no-trim") trim = false;
        else if (a == "--basic-pitch") basicPitch = next();
        else if (a == "--quality")
        {
            const auto q = next();
            opts.quality = q == "fast" ? AnalysisQuality::Fast : q == "accurate" ? AnalysisQuality::Accurate : AnalysisQuality::Balanced;
        }
        else if (a == "--source")
        {
            const auto s = next();
            opts.source = s == "vocal" ? SourceHint::Vocal : s == "keys" ? SourceHint::Keys : s == "guitar" ? SourceHint::Guitar
                        : s == "loop" ? SourceHint::Loop : SourceHint::Auto;
        }
        else if (! a.empty() && a[0] == '-') return usage();
        else path = a;
    }
    if (path.empty())
        return usage();

    AudioClip clip;
    std::string err;
    if (! so::tools::readWav (path, clip, err))
    {
        std::cerr << "so_analyze_cli: " << err << "\n";
        return 1;
    }

    BeatGrid grid;
    if (bpm)
    {
        grid.bpm = *bpm;
        grid.downbeatSec = downbeat;
    }
    else
    {
        grid = chooseFileGrid (clip, hostBpm);
    }
    if (trim)
        trimTrailingTail (clip, grid);

    if (! basicPitch.empty())
    {
#if SO_HAS_ONNX
        BasicPitchRunner::Config cfg;
        cfg.modelPath = basicPitch;
        cfg.intraOpThreads = 1;
        BasicPitchRunner runner (cfg);
        auto bp = runner.run (clip);
        if (! bp.ok)
        {
            std::cerr << "so_analyze_cli: Basic Pitch failed: " << bp.error << "\n";
            return 1;
        }
        opts.notes = std::move (bp.notes);
#else
        std::cerr << "so_analyze_cli: built without ONNX Runtime, --basic-pitch is unavailable\n";
        return 1;
#endif
    }

    AnalysisStatus st;
    const auto r = analyze (clip, grid, opts, {}, nullptr, &st);

    const double beatSec = 60.0 / r.grid.bpm;
    std::ostringstream o;
    o << "{\n  \"file\": \"" << jsonEscape (path) << "\",\n"
      << "  \"bpm\": " << num (r.grid.bpm) << ",\n  \"downbeat_sec\": " << num (r.grid.downbeatSec) << ",\n"
      << "  \"bars\": " << r.bars << ",\n"
      << "  \"tuning_cents\": " << num (r.tuningCents) << ",\n"
      << "  \"key\": \"" << jsonEscape (r.key.toString (so::theory::AccidentalStyle::Ascii)) << "\",\n"
      << "  \"key_confidence\": " << num (r.keyConfidence) << ",\n"
      << "  \"message\": \"" << jsonEscape (st.message) << "\",\n"
      << "  \"segments\": [";
    for (size_t i = 0; i < r.segments.size(); ++i)
    {
        const auto& s = r.segments[i];
        o << (i ? ",\n" : "\n") << "    { \"start_beat\": " << num (s.startBeat) << ", \"beats\": " << num (s.beats)
          << ", \"start_sec\": " << num (r.grid.beatToSec (s.startBeat))
          << ", \"end_sec\": " << num (r.grid.beatToSec (s.startBeat) + s.beats * beatSec)
          << ", " << chordJson (s.chord) << ", \"confidence\": " << num (s.confidence) << ", \"alternatives\": [";
        for (size_t k = 0; k < s.alternatives.size(); ++k)
            o << (k ? ", " : "") << "{ " << chordJson (s.alternatives[k].chord) << ", \"confidence\": "
              << num (s.alternatives[k].confidence) << " }";
        o << "] }";
    }
    o << "\n  ]\n}\n";
    std::cout << o.str();
    return 0;
}
