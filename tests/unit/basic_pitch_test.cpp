// Basic Pitch on ONNX Runtime vs. the Python reference (P1-08).
// Fixture: tests/fixtures/basic_pitch_piano.wav + .expected.json from tools/ml/make_basic_pitch_fixture.py.

#include "core/analysis/BasicPitchRunner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace so::analysis;

namespace
{
const std::string kSource = SO_SOURCE_DIR;
const std::string kModel = kSource + "/models/basic_pitch/nmp.onnx";
const std::string kWav = kSource + "/tests/fixtures/basic_pitch_piano.wav";
const std::string kExpected = kSource + "/tests/fixtures/basic_pitch_piano.expected.json";

std::string readText (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
}

uint32_t le32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t le16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

/** Minimal mono 16-bit PCM WAV reader (int16 / 32768, as soundfile/librosa). */
AudioClip readWav (const std::string& path)
{
    const std::string bytes = readText (path);
    REQUIRE (bytes.size() > 44);
    REQUIRE (bytes.compare (0, 4, "RIFF") == 0);   // fails on a Git LFS pointer: run `git lfs pull`
    const auto* d = reinterpret_cast<const unsigned char*> (bytes.data());
    AudioClip clip;
    std::size_t pos = 12;
    int channels = 0, bits = 0;
    while (pos + 8 <= bytes.size())
    {
        const uint32_t size = le32 (d + pos + 4);
        if (bytes.compare (pos, 4, "fmt ") == 0)
        {
            channels = le16 (d + pos + 10);
            clip.sampleRate = (double) le32 (d + pos + 12);
            bits = le16 (d + pos + 22);
        }
        else if (bytes.compare (pos, 4, "data") == 0)
        {
            REQUIRE (channels == 1);
            REQUIRE (bits == 16);
            const std::size_t n = size / 2;
            clip.samples.resize (n);
            for (std::size_t i = 0; i < n; ++i)
                clip.samples[i] = (float) (int16_t) le16 (d + pos + 8 + 2 * i) / 32768.0f;
            return clip;
        }
        pos += 8 + size + (size & 1u);
    }
    FAIL ("no data chunk in " << path);
    return clip;
}

/** Pulls `"key": number` values in document order (enough for the fixture JSON we write). */
std::vector<double> numbersFor (const std::string& json, const std::string& key)
{
    std::vector<double> out;
    const std::string needle = "\"" + key + "\":";
    for (std::size_t p = json.find (needle); p != std::string::npos; p = json.find (needle, p + 1))
        out.push_back (std::strtod (json.c_str() + p + needle.size(), nullptr));
    return out;
}

struct RefNote { int pitch; double start; double end; };

std::vector<RefNote> expectedNotes (const std::string& json)
{
    const auto notesAt = json.find ("\"notes\"");
    REQUIRE (notesAt != std::string::npos);
    const std::string part = json.substr (notesAt);
    const auto p = numbersFor (part, "pitch"), s = numbersFor (part, "start"), e = numbersFor (part, "end");
    REQUIRE (p.size() == s.size());
    REQUIRE (p.size() == e.size());
    std::vector<RefNote> out;
    for (std::size_t i = 0; i < p.size(); ++i)
        out.push_back ({ (int) p[i], s[i], e[i] });
    return out;
}

struct Match { int matched; double precision; double recall; double f; double maxOnsetError; double maxOffsetError; };

/** Greedy one-to-one matching: same pitch, onset within tolerance, closest first. */
Match matchNotes (const std::vector<RefNote>& ref, const std::vector<NoteEvent>& est, double tol)
{
    std::vector<bool> used (est.size(), false);
    Match m { 0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    for (const auto& r : ref)
    {
        int best = -1;
        double bestErr = tol;
        for (std::size_t j = 0; j < est.size(); ++j)
        {
            if (used[j] || est[j].pitch != r.pitch)
                continue;
            const double err = std::abs (est[j].startSec - r.start);
            if (err <= bestErr)
            {
                bestErr = err;
                best = (int) j;
            }
        }
        if (best >= 0)
        {
            used[(std::size_t) best] = true;
            ++m.matched;
            m.maxOnsetError = std::max (m.maxOnsetError, bestErr);
            m.maxOffsetError = std::max (m.maxOffsetError, std::abs (est[(std::size_t) best].endSec - r.end));
        }
    }
    m.precision = est.empty() ? 0.0 : (double) m.matched / (double) est.size();
    m.recall = ref.empty() ? 0.0 : (double) m.matched / (double) ref.size();
    m.f = m.precision + m.recall > 0.0 ? 2.0 * m.precision * m.recall / (m.precision + m.recall) : 0.0;
    return m;
}

BasicPitchRunner makeRunner() { return BasicPitchRunner ({ kModel, {}, 2 }); }
} // namespace

TEST_CASE ("Basic Pitch posteriorgram matches the Python reference", "[ml][basicpitch]")
{
    const auto clip = readWav (kWav);
    REQUIRE (clip.sampleRate == 22050.0);
    const std::string json = readText (kExpected);

    auto runner = makeRunner();
    REQUIRE (runner.prepare());
    const auto result = runner.run (clip);
    INFO (result.error);
    REQUIRE (result.ok);

    const auto& pg = result.posteriorgram;
    REQUIRE (pg.numFrames == (int) numbersFor (json, "frames").at (0));
    REQUIRE (pg.values.size() == (std::size_t) pg.numFrames * 88);
    REQUIRE (pg.frameTimes.size() == (std::size_t) pg.numFrames);

    double sum = 0.0;
    for (float v : pg.values)
        sum += v;
    const double refSum = numbersFor (json, "sum").at (0);
    CHECK_THAT (sum, Catch::Matchers::WithinRel (refSum, 1e-3));

    // Sampled rows ("rows": {"<frame>": [88 values]}).
    const auto rowsAt = json.find ("\"rows\"");
    REQUIRE (rowsAt != std::string::npos);
    double maxErr = 0.0;
    int rowsChecked = 0;
    for (std::size_t p = json.find ("\"", rowsAt + 7); p != std::string::npos && p < json.find ("\"notes\""); )
    {
        const auto q = json.find ('"', p + 1);
        const int frame = std::stoi (json.substr (p + 1, q - p - 1));
        auto lb = json.find ('[', q);
        const auto rb = json.find (']', lb);
        std::stringstream ss (json.substr (lb + 1, rb - lb - 1));
        std::string tok;
        int col = 0;
        while (std::getline (ss, tok, ','))
            maxErr = std::max (maxErr, std::abs ((double) pg.at (frame, col++) - std::stod (tok)));
        CHECK (col == 88);
        ++rowsChecked;
        p = json.find ('"', rb);
    }
    std::printf ("[basic pitch] posteriorgram: %d frames, sum %.4f (Python %.4f), max sampled-row error %.2e\n",
                 pg.numFrames, sum, refSum, maxErr);
    CHECK (rowsChecked == 5);
    CHECK (maxErr < 1e-3);
}

TEST_CASE ("Basic Pitch notes match the Python reference (pitch exact, onset 20 ms, F >= 0.9)", "[ml][basicpitch]")
{
    const auto clip = readWav (kWav);
    const auto ref = expectedNotes (readText (kExpected));
    REQUIRE (ref.size() > 20);

    auto runner = makeRunner();
    const auto result = runner.run (clip);
    INFO (result.error);
    REQUIRE (result.ok);

    const auto m = matchNotes (ref, result.notes, 0.020);
    std::printf ("[basic pitch] C++ %zu notes, Python %zu, matched %d, P %.3f R %.3f F %.3f, max onset error %.2f ms, max offset error %.2f ms\n",
                 result.notes.size(), ref.size(), m.matched, m.precision, m.recall, m.f, m.maxOnsetError * 1000.0,
                 m.maxOffsetError * 1000.0);
    CHECK (m.f >= 0.90);
    CHECK (m.maxOnsetError <= 0.020);

    for (const auto& n : result.notes)
    {
        CHECK (n.pitch >= 21);
        CHECK (n.pitch <= 108);
        CHECK (n.endSec > n.startSec);
        CHECK (n.velocity >= 0.0f);
        CHECK (n.velocity <= 1.0f);
        CHECK (n.confidence >= 0.0f);
        CHECK (n.confidence <= 1.0f);
    }
    REQUIRE (std::is_sorted (result.notes.begin(), result.notes.end(),
                             [] (const NoteEvent& a, const NoteEvent& b) { return a.startSec < b.startSec; }));
}

TEST_CASE ("Basic Pitch is deterministic", "[ml][basicpitch]")
{
    const auto clip = readWav (kWav);
    auto runner = makeRunner();
    const auto a = runner.run (clip);
    const auto b = runner.run (clip);
    REQUIRE (a.ok);
    REQUIRE (b.ok);
    REQUIRE (a.notes.size() == b.notes.size());
    for (std::size_t i = 0; i < a.notes.size(); ++i)
    {
        CHECK (a.notes[i].pitch == b.notes[i].pitch);
        CHECK (a.notes[i].startSec == b.notes[i].startSec);
        CHECK (a.notes[i].endSec == b.notes[i].endSec);
        CHECK (a.notes[i].velocity == b.notes[i].velocity);
    }
    CHECK (a.posteriorgram.values == b.posteriorgram.values);
}

TEST_CASE ("Basic Pitch resamples non-22.05 kHz input", "[ml][basicpitch]")
{
    const auto clip = readWav (kWav);
    AudioClip hi;
    hi.sampleRate = 44100.0;
    hi.samples = detail::resampleSinc (clip.samples.data(), clip.samples.size(), 22050.0, 44100.0);
    REQUIRE (hi.samples.size() == clip.samples.size() * 2);

    auto runner = makeRunner();
    const auto result = runner.run (hi);
    REQUIRE (result.ok);
    const auto m = matchNotes (expectedNotes (readText (kExpected)), result.notes, 0.020);
    std::printf ("[basic pitch] 44.1 kHz round trip: F %.3f, max onset error %.2f ms\n", m.f, m.maxOnsetError * 1000.0);
    CHECK (m.f >= 0.90);
}

TEST_CASE ("Basic Pitch shares one session per model across runners", "[ml][basicpitch]")
{
    {
        auto a = makeRunner();
        auto b = makeRunner();
        REQUIRE (a.prepare());
        REQUIRE (b.prepare());
        CHECK (BasicPitchRunner::sharedSessionCount() == 1);
    }
    CHECK (BasicPitchRunner::sharedSessionCount() == 0);
}

TEST_CASE ("Basic Pitch honours the cancel flag and edge cases", "[ml][basicpitch]")
{
    auto runner = makeRunner();
    const auto clip = readWav (kWav);

    std::atomic<bool> cancel { true };
    std::atomic<float> progress { 0.0f };
    const auto cancelled = runner.run (clip, {}, &cancel, &progress);
    CHECK (cancelled.cancelled);
    CHECK_FALSE (cancelled.ok);
    CHECK (cancelled.notes.empty());

    cancel = false;
    const auto full = runner.run (clip, {}, &cancel, &progress);
    CHECK (full.ok);
    CHECK (progress.load() == 1.0f);

    const auto empty = runner.run (AudioClip {});
    CHECK (empty.ok);
    CHECK (empty.notes.empty());
    CHECK (empty.posteriorgram.numFrames == 0);

    AudioClip silence;
    silence.sampleRate = 22050.0;
    silence.samples.assign (22050 * 3, 0.0f);
    const auto quiet = runner.run (silence);
    CHECK (quiet.ok);
    CHECK (quiet.notes.empty());

    BasicPitchRunner missing ({ kSource + "/models/basic_pitch/does-not-exist.onnx", {}, 1 });
    CHECK_FALSE (missing.prepare());
    CHECK_FALSE (missing.lastError().empty());
    CHECK_FALSE (missing.run (clip).ok);
}

TEST_CASE ("Note creation port: frame times follow model_frames_to_time", "[ml][basicpitch]")
{
    const auto t = basicPitchFrameTimes (400);
    CHECK (t[0] == 0.0);
    CHECK_THAT (t[171], Catch::Matchers::WithinAbs (171.0 * 256.0 / 22050.0, 1e-12));
    // From frame 172 on, the reference subtracts (256/22050)*(172 - 43844/256) + 0.0018 per window.
    const double offset = (256.0 / 22050.0) * (172.0 - 43844.0 / 256.0) + 0.0018;
    CHECK_THAT (t[172], Catch::Matchers::WithinAbs (172.0 * 256.0 / 22050.0 - offset, 1e-12));
    CHECK_THAT (t[399], Catch::Matchers::WithinAbs (399.0 * 256.0 / 22050.0 - 2.0 * offset, 1e-12));
}

// Not run by default (shared CI and dev VMs are noisy). Run: so_ml_tests "[.timing]" -s
TEST_CASE ("Basic Pitch timing for 64 s of audio", "[.timing]")
{
    const auto clip = readWav (kWav);
    AudioClip longClip;
    longClip.sampleRate = clip.sampleRate;
    while (longClip.samples.size() < (std::size_t) (64.0 * clip.sampleRate))
        longClip.samples.insert (longClip.samples.end(), clip.samples.begin(), clip.samples.end());
    longClip.samples.resize ((std::size_t) (64.0 * clip.sampleRate));

    AudioClip clip44;
    clip44.sampleRate = 44100.0;
    clip44.samples = detail::resampleSinc (longClip.samples.data(), longClip.samples.size(), 22050.0, 44100.0);

    auto runner = makeRunner();
    REQUIRE (runner.prepare());
    for (const auto* c : { &longClip, &clip44 })
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = runner.run (*c);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        REQUIRE (r.ok);
        std::printf ("[basic pitch] 64 s at %.0f Hz: %.0f ms total (%zu notes, %d frames)\n",
                     c->sampleRate, ms, r.notes.size(), r.posteriorgram.numFrames);
    }
}
