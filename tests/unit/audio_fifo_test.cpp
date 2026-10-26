#include <catch2/catch_test_macros.hpp>

#include "core/audio/SpscAudioFifo.h"

#include <algorithm>
#include <thread>
#include <vector>

using so::audio::SpscAudioFifo;

TEST_CASE ("FIFO push/pop preserves order across wrap-around", "[audio][fifo]")
{
    SpscAudioFifo fifo;
    fifo.reset (2, 10);

    std::vector<float> l (7), r (7), outL (7), outR (7);
    float counter = 0.0f;
    for (int round = 0; round < 20; ++round)
    {
        for (std::size_t i = 0; i < 7; ++i) { l[i] = counter; r[i] = -counter; counter += 1.0f; }
        const float* src[2] { l.data(), r.data() };
        REQUIRE (fifo.push (src, 2, 7) == 7);
        REQUIRE (fifo.availableToRead() == 7);

        float* dst[2] { outL.data(), outR.data() };
        REQUIRE (fifo.pop (dst, 7) == 7);
        CHECK (outL == l);
        CHECK (outR == r);
    }
}

TEST_CASE ("FIFO refuses to overfill and reports partial pushes", "[audio][fifo]")
{
    SpscAudioFifo fifo;
    fifo.reset (1, 8);
    std::vector<float> data (12, 1.0f);
    const float* src[1] { data.data() };

    CHECK (fifo.push (src, 1, 12) == 8);
    CHECK (fifo.availableToWrite() == 0);
    CHECK (fifo.push (src, 1, 1) == 0);
}

TEST_CASE ("FIFO repeats a mono source into both channels", "[audio][fifo]")
{
    SpscAudioFifo fifo;
    fifo.reset (2, 4);
    const float mono[3] { 0.1f, 0.2f, 0.3f };
    const float* src[1] { mono };
    REQUIRE (fifo.push (src, 1, 3) == 3);

    float a[3] {}, b[3] {};
    float* dst[2] { a, b };
    REQUIRE (fifo.pop (dst, 3) == 3);
    CHECK (b[2] == 0.3f);
    CHECK (a[0] == 0.1f);
}

TEST_CASE ("FIFO is consistent with a concurrent producer and consumer", "[audio][fifo]")
{
    SpscAudioFifo fifo;
    fifo.reset (1, 257);
    constexpr int total = 200000;

    std::thread producer ([&]
    {
        float block[64];
        int next = 0;
        while (next < total)
        {
            const int n = std::min (64, total - next);
            for (int i = 0; i < n; ++i)
                block[i] = (float) (next + i);
            const float* src[1] { block };
            next += (int) fifo.push (src, 1, (std::size_t) n);   // retry what did not fit
        }
    });

    int expected = 0;
    bool ordered = true;
    float out[100];
    while (expected < total)
    {
        float* dst[1] { out };
        const auto got = fifo.pop (dst, 100);
        for (std::size_t i = 0; i < got; ++i)
            ordered = ordered && out[i] == (float) expected++;
    }
    producer.join();
    CHECK (ordered);
    CHECK (fifo.availableToRead() == 0);
}
