#include "BasicPitchRunner.h"

// The ONNX Runtime C++ API keeps one global OrtApi pointer. With manual init we decide where it
// comes from: an explicitly loaded library (full path) or the linked import (ADR 0009).
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace so::analysis
{
namespace bp = basicpitch;

namespace
{
    constexpr const char* kInputName = "serving_default_input_2:0";
    constexpr const char* kNoteOutput = "StatefulPartitionedCall:1";
    constexpr const char* kOnsetOutput = "StatefulPartitionedCall:2";
    constexpr int kMaxFreqIdx = 87;
    constexpr double kMagicAlignmentOffset = 0.0018;

    //==============================================================================================
    // Runtime library loading
    //==============================================================================================
    using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*) ();

#if defined(_WIN32)
    /** Directory of the module (plugin DLL or test exe) that contains this code. */
    std::filesystem::path thisModuleDirectory()
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR> (&thisModuleDirectory), &module))
            return {};
        std::wstring buffer (32768, L'\0');
        const DWORD len = GetModuleFileNameW (module, buffer.data(), (DWORD) buffer.size());
        if (len == 0 || len >= buffer.size())
            return {};
        buffer.resize (len);
        return std::filesystem::path (buffer).parent_path();
    }

    GetApiBaseFn loadFromPath (const std::filesystem::path& path, std::string& err)
    {
        // A full path loads exactly this file (never a same-named DLL already in the process or in
        // System32); LOAD_WITH_ALTERED_SEARCH_PATH resolves its own dependencies from its folder.
        HMODULE h = LoadLibraryExW (path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h == nullptr)
        {
            err = "could not load " + path.string() + " (error " + std::to_string (GetLastError()) + ")";
            return nullptr;
        }
        auto fn = reinterpret_cast<GetApiBaseFn> (reinterpret_cast<void*> (GetProcAddress (h, "OrtGetApiBase")));
        if (fn == nullptr)
            err = path.string() + " does not export OrtGetApiBase";
        return fn;   // the handle stays loaded for the life of the process (ADR 0009)
    }
#else
    GetApiBaseFn loadFromPath (const std::filesystem::path& path, std::string& err)
    {
        void* h = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr)
        {
            const char* msg = dlerror();
            err = std::string ("could not load ") + path.string() + ": " + (msg != nullptr ? msg : "unknown error");
            return nullptr;
        }
        auto fn = reinterpret_cast<GetApiBaseFn> (dlsym (h, "OrtGetApiBase"));
        if (fn == nullptr)
            err = path.string() + " does not export OrtGetApiBase";
        return fn;   // never dlclose'd: unloading ORT with live thread pools is unsafe
    }
#endif

    std::filesystem::path utf8Path (const std::string& s)
    {
        return std::filesystem::path (std::u8string (reinterpret_cast<const char8_t*> (s.data()), s.size()));
    }

    /** Initialises Ort::Global<void>::api_ once per process. Caller holds the registry mutex. */
    bool initialiseApi (const std::string& libraryPath, std::string& err)
    {
        static const OrtApi* api = nullptr;
        if (api != nullptr)
            return true;

        GetApiBaseFn getBase = nullptr;
        if (! libraryPath.empty())
        {
            getBase = loadFromPath (utf8Path (libraryPath), err);
            if (getBase == nullptr)
                return false;
        }
#if defined(_WIN32)
        if (getBase == nullptr)
        {
            const auto local = thisModuleDirectory() / L"onnxruntime.dll";
            std::error_code ec;
            if (! local.empty() && std::filesystem::exists (local, ec))
            {
                getBase = loadFromPath (local, err);
                if (getBase == nullptr)
                    return false;
            }
        }
#endif
        if (getBase == nullptr)
            getBase = &OrtGetApiBase;   // linked import (rpath on macOS/Linux, delay-load on Windows)

        const OrtApiBase* base = getBase();
        api = base != nullptr ? base->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            err = std::string ("ONNX Runtime ") + (base != nullptr ? base->GetVersionString() : "?")
                + " does not provide API version " + std::to_string (ORT_API_VERSION);
            return false;
        }
        Ort::InitApi (api);
        return true;
    }

    bool readFile (const std::string& path, std::vector<char>& out, std::string& err)
    {
        std::ifstream in (utf8Path (path), std::ios::binary);
        if (! in)
        {
            err = "cannot open model file " + path;
            return false;
        }
        out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
        static constexpr char lfsPrefix[] = "version https://git-lfs";
        if (out.size() < 1024 && out.size() >= sizeof (lfsPrefix) - 1
            && std::memcmp (out.data(), lfsPrefix, sizeof (lfsPrefix) - 1) == 0)
        {
            err = path + " is a Git LFS pointer; run `git lfs pull`";
            return false;
        }
        if (out.empty())
        {
            err = "model file is empty: " + path;
            return false;
        }
        return true;
    }
} // namespace

//==================================================================================================
// Shared env + sessions (NFR-15)
//==================================================================================================
struct BasicPitchRunner::Shared
{
    std::shared_ptr<Ort::Env> env;
    Ort::Session session { nullptr };
};

namespace
{
    struct Registry
    {
        std::mutex mutex;
        std::weak_ptr<Ort::Env> env;
        std::map<std::string, std::weak_ptr<BasicPitchRunner::Shared>> sessions;
    };

    Registry& registry()
    {
        static auto* r = new Registry();   // intentionally leaked: no static-destruction order issues
        return *r;
    }

    bool hasNamed (const std::vector<Ort::AllocatedStringPtr>& names, const char* wanted)
    {
        return std::any_of (names.begin(), names.end(),
                            [wanted] (const Ort::AllocatedStringPtr& n) { return std::strcmp (n.get(), wanted) == 0; });
    }
} // namespace

BasicPitchRunner::BasicPitchRunner (Config c) : config (std::move (c))
{
    config.intraOpThreads = std::clamp (config.intraOpThreads, 1, 2);
}

BasicPitchRunner::~BasicPitchRunner() = default;

int BasicPitchRunner::sharedSessionCount()
{
    auto& reg = registry();
    std::lock_guard<std::mutex> lock (reg.mutex);
    int n = 0;
    for (auto& [path, weak] : reg.sessions)
        n += weak.expired() ? 0 : 1;
    return n;
}

bool BasicPitchRunner::prepare()
{
    if (shared != nullptr)
        return true;

    auto& reg = registry();
    std::lock_guard<std::mutex> lock (reg.mutex);

    if (auto existing = reg.sessions[config.modelPath].lock())
    {
        shared = std::move (existing);
        error.clear();
        return true;
    }

    try
    {
        if (! initialiseApi (config.runtimeLibraryPath, error))
            return false;

        std::vector<char> model;
        if (! readFile (config.modelPath, model, error))
            return false;

        auto env = reg.env.lock();
        if (env == nullptr)
        {
            env = std::make_shared<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "SecondOpinion");
            reg.env = env;
        }

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (config.intraOpThreads);
        opts.SetInterOpNumThreads (1);
        opts.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.AddConfigEntry ("session.intra_op.allow_spinning", "0");   // do not burn DAW CPU

        auto s = std::make_shared<Shared>();
        s->env = env;
        s->session = Ort::Session (*env, model.data(), model.size(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        std::vector<Ort::AllocatedStringPtr> ins, outs;
        for (std::size_t i = 0; i < s->session.GetInputCount(); ++i)
            ins.push_back (s->session.GetInputNameAllocated (i, alloc));
        for (std::size_t i = 0; i < s->session.GetOutputCount(); ++i)
            outs.push_back (s->session.GetOutputNameAllocated (i, alloc));
        if (! hasNamed (ins, kInputName) || ! hasNamed (outs, kNoteOutput) || ! hasNamed (outs, kOnsetOutput))
        {
            error = "model does not have the Basic Pitch inputs/outputs: " + config.modelPath;
            return false;
        }

        reg.sessions[config.modelPath] = s;
        shared = std::move (s);
        error.clear();
        return true;
    }
    catch (const Ort::Exception& e)
    {
        error = std::string ("ONNX Runtime: ") + e.what();
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    return false;
}

//==================================================================================================
// Inference (basic_pitch/inference.py run_inference)
//==================================================================================================
BasicPitchResult BasicPitchRunner::run (const AudioClip& clip, const BasicPitchOptions& options,
                                        const std::atomic<bool>* cancel, std::atomic<float>* progress)
{
    if (clip.sampleRate == (double) bp::kSampleRate || clip.samples.empty())
        return run22k (clip.samples.data(), clip.samples.size(), options, cancel, progress);

    if (! (clip.sampleRate > 0.0))
    {
        BasicPitchResult r;
        r.error = "invalid sample rate";
        return r;
    }
    const auto resampled = detail::resampleSinc (clip.samples.data(), clip.samples.size(),
                                                 clip.sampleRate, (double) bp::kSampleRate);
    return run22k (resampled.data(), resampled.size(), options, cancel, progress);
}

BasicPitchResult BasicPitchRunner::run22k (const float* samples, std::size_t numSamples,
                                           const BasicPitchOptions& options,
                                           const std::atomic<bool>* cancel, std::atomic<float>* progress)
{
    BasicPitchResult result;
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load (std::memory_order_relaxed); };

    if (! prepare())
    {
        result.error = error;
        return result;
    }

    // get_audio_input: prepend overlap/2 zeros, then hop through the padded signal.
    const std::size_t pad = bp::kOverlapSamples / 2;
    const std::size_t paddedLen = numSamples + pad;
    const std::size_t numWindows = numSamples == 0 ? 0 : (paddedLen + bp::kWindowHop - 1) / bp::kWindowHop;

    // unwrap_output: keep frames [15, 157) of every window, then trim.
    constexpr int keepPerWindow = bp::kFramesPerWindow - bp::kOverlapFrames;   // 142
    constexpr int olap = bp::kOverlapFrames / 2;
    const int numFrames = (int) ((double) numSamples / (double) bp::kWindowHop * (double) keepPerWindow);

    std::vector<float> note ((std::size_t) numWindows * keepPerWindow * bp::kNumPitches);
    std::vector<float> onset (note.size());

    std::vector<float> window ((std::size_t) bp::kAudioSamples);
    std::vector<float> outNote ((std::size_t) bp::kFramesPerWindow * bp::kNumPitches);
    std::vector<float> outOnset (outNote.size());

    try
    {
        const auto mem = Ort::MemoryInfo::CreateCpu (OrtDeviceAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 3> inShape { 1, bp::kAudioSamples, 1 };
        const std::array<int64_t, 3> outShape { 1, bp::kFramesPerWindow, bp::kNumPitches };
        const char* inNames[] = { kInputName };
        const char* outNames[] = { kNoteOutput, kOnsetOutput };
        Ort::RunOptions runOptions;

        for (std::size_t w = 0; w < numWindows; ++w)
        {
            if (cancelled())
            {
                result.cancelled = true;
                return result;
            }

            // Window w covers padded[w*hop, w*hop + N); padded[i] = 0 for i < pad.
            std::fill (window.begin(), window.end(), 0.0f);
            const std::size_t start = w * (std::size_t) bp::kWindowHop;
            for (std::size_t j = 0; j < (std::size_t) bp::kAudioSamples; ++j)
            {
                const std::size_t p = start + j;
                if (p >= paddedLen)
                    break;
                if (p >= pad)
                    window[j] = samples[p - pad];
            }

            Ort::Value input = Ort::Value::CreateTensor<float> (mem, window.data(), window.size(), inShape.data(), inShape.size());
            std::array<Ort::Value, 2> outputs {
                Ort::Value::CreateTensor<float> (mem, outNote.data(), outNote.size(), outShape.data(), outShape.size()),
                Ort::Value::CreateTensor<float> (mem, outOnset.data(), outOnset.size(), outShape.data(), outShape.size())
            };
            shared->session.Run (runOptions, inNames, &input, 1, outNames, outputs.data(), outputs.size());

            const std::size_t rowFloats = (std::size_t) bp::kNumPitches;
            const std::size_t dst = w * (std::size_t) keepPerWindow * rowFloats;
            std::copy_n (outNote.begin() + (std::ptrdiff_t) (olap * rowFloats), keepPerWindow * rowFloats, note.begin() + (std::ptrdiff_t) dst);
            std::copy_n (outOnset.begin() + (std::ptrdiff_t) (olap * rowFloats), keepPerWindow * rowFloats, onset.begin() + (std::ptrdiff_t) dst);

            if (progress != nullptr)
                progress->store ((float) (w + 1) / (float) numWindows, std::memory_order_relaxed);
        }
    }
    catch (const Ort::Exception& e)
    {
        result.error = std::string ("ONNX Runtime: ") + e.what();
        return result;
    }

    const int framesAvailable = (int) (numWindows * keepPerWindow);
    const int frames = std::clamp (numFrames, 0, framesAvailable);
    note.resize ((std::size_t) frames * bp::kNumPitches);
    onset.resize (note.size());

    if (cancelled())
    {
        result.cancelled = true;
        return result;
    }

    result.notes = basicPitchNotesFromPosteriors (note.data(), onset.data(), frames, options);
    result.posteriorgram.numFrames = frames;
    result.posteriorgram.values = std::move (note);
    result.posteriorgram.frameTimes = basicPitchFrameTimes (frames);
    result.ok = true;
    return result;
}

//==================================================================================================
// Note creation (basic_pitch/note_creation.py)
//==================================================================================================
std::vector<double> basicPitchFrameTimes (int numFrames)
{
    // model_frames_to_time: frame * hop / sr minus a per-window offset. ANNOT_N_FRAMES (172) is
    // used here even though windows contribute 142 frames; that is the reference behaviour.
    const double windowOffset = ((double) bp::kFftHop / (double) bp::kSampleRate)
                                    * ((double) bp::kFramesPerWindow - (double) bp::kAudioSamples / (double) bp::kFftHop)
                                + kMagicAlignmentOffset;
    std::vector<double> t ((std::size_t) std::max (0, numFrames));
    for (int i = 0; i < numFrames; ++i)
    {
        const double original = (double) ((int64_t) i * bp::kFftHop) / (double) bp::kSampleRate;
        t[(std::size_t) i] = original - windowOffset * std::floor ((double) i / (double) bp::kFramesPerWindow);
    }
    return t;
}

std::vector<NoteEvent> basicPitchNotesFromPosteriors (const float* frames, const float* onsetsIn,
                                                      int numFrames, const BasicPitchOptions& o)
{
    constexpr int P = bp::kNumPitches;
    std::vector<NoteEvent> notes;
    if (numFrames <= 0)
        return notes;

    const int T = numFrames;
    const std::size_t cells = (std::size_t) T * P;
    auto idx = [] (int t, int f) { return (std::size_t) t * P + (std::size_t) f; };

    // min_note_len = round(ms / 1000 * fps), numpy rounds half to even (nearbyint does too).
    const int minNoteLen = (int) std::nearbyint (o.minNoteLengthMs / 1000.0 * ((double) bp::kSampleRate / (double) bp::kFftHop));

    // get_infered_onsets (float64 like numpy).
    std::vector<double> onsets (cells);
    for (std::size_t i = 0; i < cells; ++i)
        onsets[i] = (double) onsetsIn[i];

    if (o.inferOnsets)
    {
        constexpr int nDiff = 2;
        std::vector<double> diff (cells, 0.0);
        double maxDiff = 0.0;
        for (int t = 0; t < T; ++t)
            for (int f = 0; f < P; ++f)
            {
                double m = 0.0;
                for (int n = 1; n <= nDiff; ++n)
                {
                    const double prev = t - n >= 0 ? (double) frames[idx (t - n, f)] : 0.0;
                    const double d = (double) frames[idx (t, f)] - prev;
                    m = n == 1 ? d : std::min (m, d);
                }
                if (m < 0.0 || t < nDiff)
                    m = 0.0;
                diff[idx (t, f)] = m;
                maxDiff = std::max (maxDiff, m);
            }
        float maxOnsetF = onsetsIn[0];
        for (std::size_t i = 1; i < cells; ++i)
            maxOnsetF = std::max (maxOnsetF, onsetsIn[i]);
        const double maxOnset = (double) maxOnsetF;
        // numpy divides by zero into NaN here, which then loses every comparison; skip instead.
        if (maxDiff > 0.0)
            for (std::size_t i = 0; i < cells; ++i)
                onsets[i] = std::max (onsets[i], maxOnset * diff[i] / maxDiff);
    }

    // scipy.signal.argrelmax along time (strict, edges never peaks), then threshold. np.where
    // yields (t, f) in row-major order; the reference walks it backwards.
    struct Onset { int t; int f; double value; };
    std::vector<Onset> peaks;
    for (int t = 1; t < T - 1; ++t)
        for (int f = 0; f < P; ++f)
        {
            const double v = onsets[idx (t, f)];
            if (v > onsets[idx (t - 1, f)] && v > onsets[idx (t + 1, f)] && v >= o.onsetThreshold)
                peaks.push_back ({ t, f, v });
        }

    std::vector<float> remaining (frames, frames + cells);
    const double thr = o.frameThreshold;
    const int tol = o.energyTolerance;

    auto zeroAround = [&] (int t, int f)
    {
        remaining[idx (t, f)] = 0.0f;
        if (f < kMaxFreqIdx)
            remaining[idx (t, f + 1)] = 0.0f;
        if (f > 0)
            remaining[idx (t, f - 1)] = 0.0f;
    };
    auto meanFrames = [&] (int s, int e, int f)
    {
        double sum = 0.0;
        for (int t = s; t < e; ++t)
            sum += (double) frames[idx (t, f)];
        return e > s ? (float) (sum / (double) (e - s)) : 0.0f;
    };
    auto addNote = [&] (int s, int e, int f, float amplitude, float confidence)
    {
        NoteEvent n;
        n.pitch = f + bp::kLowestMidi;
        n.startSec = (double) s;   // frame indices for now, converted below
        n.endSec = (double) e;
        n.velocity = std::clamp (amplitude, 0.0f, 1.0f);
        n.confidence = std::clamp (confidence, 0.0f, 1.0f);
        notes.push_back (n);
    };

    for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
    {
        const int start = it->t;
        const int f = it->f;
        if (start >= T - 1)
            continue;

        int i = start + 1;
        int k = 0;
        while (i < T - 1 && k < tol)
        {
            if ((double) remaining[idx (i, f)] < thr)
                ++k;
            else
                k = 0;
            ++i;
        }
        i -= k;

        if (i - start <= minNoteLen)
            continue;

        for (int t = start; t < i; ++t)
            zeroAround (t, f);

        addNote (start, i, f, meanFrames (start, i, f), (float) it->value);
    }

    if (o.melodiaTrick)
    {
        // Global argmax in row-major order (numpy tie-break: first occurrence), kept fast with a
        // per-row maximum cache that is refreshed for every row a note touches.
        std::vector<float> rowMax ((std::size_t) T);
        std::vector<int> rowArg ((std::size_t) T);
        auto refreshRow = [&] (int t)
        {
            const float* row = remaining.data() + idx (t, 0);
            int best = 0;
            for (int f = 1; f < P; ++f)
                if (row[f] > row[best])
                    best = f;
            rowMax[(std::size_t) t] = row[best];
            rowArg[(std::size_t) t] = best;
        };
        for (int t = 0; t < T; ++t)
            refreshRow (t);

        for (;;)
        {
            int tMid = 0;
            for (int t = 1; t < T; ++t)
                if (rowMax[(std::size_t) t] > rowMax[(std::size_t) tMid])
                    tMid = t;
            if (! ((double) rowMax[(std::size_t) tMid] > thr))
                break;

            const int f = rowArg[(std::size_t) tMid];
            remaining[idx (tMid, f)] = 0.0f;

            int i = tMid + 1;
            int k = 0;
            while (i < T - 1 && k < tol)
            {
                if ((double) remaining[idx (i, f)] < thr)
                    ++k;
                else
                    k = 0;
                zeroAround (i, f);
                ++i;
            }
            const int iEnd = i - 1 - k;
            const int touchedHi = i - 1;

            i = tMid - 1;
            k = 0;
            while (i > 0 && k < tol)
            {
                if ((double) remaining[idx (i, f)] < thr)
                    ++k;
                else
                    k = 0;
                zeroAround (i, f);
                --i;
            }
            const int iStart = i + 1 + k;
            const int touchedLo = i + 1;

            for (int t = std::min (touchedLo, tMid); t <= std::max (touchedHi, tMid); ++t)
                refreshRow (t);

            if (iEnd - iStart <= minNoteLen)
                continue;

            const float amplitude = meanFrames (iStart, iEnd, f);
            addNote (iStart, iEnd, f, amplitude, amplitude);
        }
    }

    const auto times = basicPitchFrameTimes (T);
    for (auto& n : notes)
    {
        n.startSec = times[(std::size_t) n.startSec];
        n.endSec = times[(std::size_t) n.endSec];
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
    {
        if (a.startSec != b.startSec)
            return a.startSec < b.startSec;
        return a.pitch < b.pitch;
    });
    return notes;
}

//==================================================================================================
// Resampler
//==================================================================================================
namespace detail
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            const double q = x * x / 4.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= q / ((double) k * (double) k);
                sum += term;
                if (term < sum * 1e-17)
                    break;
            }
            return sum;
        }
    } // namespace

    std::vector<float> resampleSinc (const float* input, std::size_t numSamples, double sourceRate, double targetRate)
    {
        if (numSamples == 0 || ! (sourceRate > 0.0) || ! (targetRate > 0.0))
            return {};
        if (sourceRate == targetRate)
            return std::vector<float> (input, input + numSamples);

        constexpr int zeroCrossings = 24;      // per side, at the cutoff frequency
        constexpr int tableRes = 512;          // kernel samples per zero crossing
        constexpr double beta = 8.6;           // Kaiser, about 90 dB stopband
        const double ratio = targetRate / sourceRate;
        const double cutoff = 0.95 * std::min (1.0, ratio);   // fraction of source Nyquist

        // Kernel h(x) for x in source samples scaled by cutoff, tabulated on [0, zeroCrossings].
        static const std::vector<double> table = []
        {
            const int n = zeroCrossings * tableRes + 2;
            std::vector<double> t ((std::size_t) n);
            const double i0b = besselI0 (beta);
            for (int i = 0; i < n; ++i)
            {
                const double x = (double) i / tableRes;
                const double sinc = i == 0 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                const double r = x / zeroCrossings;
                const double win = r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                t[(std::size_t) i] = sinc * win;
            }
            return t;
        }();
        auto kernel = [&] (double x)
        {
            const double pos = std::abs (x) * tableRes;
            const auto i = (std::size_t) pos;
            if (i + 1 >= table.size())
                return 0.0;
            const double frac = pos - (double) i;
            return table[i] + frac * (table[i + 1] - table[i]);
        };

        const auto outLen = (std::size_t) std::llround (std::ceil ((double) numSamples * ratio));
        std::vector<float> out (outLen);
        const double halfWidth = zeroCrossings / cutoff;   // in source samples
        for (std::size_t n = 0; n < outLen; ++n)
        {
            const double centre = (double) n / ratio;
            const auto lo = (int64_t) std::ceil (centre - halfWidth);
            const auto hi = (int64_t) std::floor (centre + halfWidth);
            double acc = 0.0;
            for (int64_t m = std::max<int64_t> (lo, 0); m <= std::min<int64_t> (hi, (int64_t) numSamples - 1); ++m)
                acc += (double) input[m] * kernel (((double) m - centre) * cutoff);
            out[n] = (float) (acc * cutoff);
        }
        return out;
    }
} // namespace detail

} // namespace so::analysis
