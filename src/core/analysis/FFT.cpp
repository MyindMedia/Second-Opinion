#include "FFT.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace so::analysis
{

FFT::FFT (int order_) : order (order_), n (1 << (order_ > 0 && order_ <= 20 ? order_ : 1))
{
    if (order_ < 1 || order_ > 20)
        throw std::invalid_argument ("FFT order must be in 1..20");

    twiddles.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
    {
        const double a = -2.0 * std::numbers::pi * (double) k / (double) n;
        twiddles[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
    }

    bitReverse.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        unsigned r = 0;
        for (int b = 0; b < order; ++b)
            if ((i >> b) & 1)
                r |= 1u << (order - 1 - b);
        bitReverse[(size_t) i] = r;
    }
}

int FFT::orderFor (int minSize) noexcept
{
    int o = 1;
    while ((1 << o) < minSize && o < 20)
        ++o;
    return o;
}

void FFT::forward (Complex* data) const noexcept { transform (data, false); }
void FFT::inverse (Complex* data) const noexcept { transform (data, true); }

void FFT::transform (Complex* d, bool inv) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto j = (int) bitReverse[(size_t) i];
        if (i < j)
            std::swap (d[i], d[j]);
    }

    // Work on the interleaved float representation (std::complex<float> is layout-compatible
    // with float[2]) so the butterfly stays a plain multiply-add without library NaN checks.
    auto* f = reinterpret_cast<float*> (d);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;

        for (int i = 0; i < n; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                const Complex& w = twiddles[(size_t) (j * step)];
                const float wr = w.real();
                const float wi = inv ? -w.imag() : w.imag();

                float* a = f + 2 * (i + j);
                float* b = f + 2 * (i + j + half);

                const float br = b[0] * wr - b[1] * wi;
                const float bi = b[0] * wi + b[1] * wr;

                b[0] = a[0] - br;
                b[1] = a[1] - bi;
                a[0] += br;
                a[1] += bi;
            }
        }
    }

    if (inv)
    {
        const float s = 1.0f / (float) n;
        for (int i = 0; i < 2 * n; ++i)
            f[i] *= s;
    }
}

void FFT::realMagnitudes (const float* in, float* out, std::vector<Complex>& scratch) const
{
    scratch.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        scratch[(size_t) i] = { in[i], 0.0f };

    forward (scratch.data());

    for (int k = 0; k <= n / 2; ++k)
        out[k] = std::abs (scratch[(size_t) k]);
}

} // namespace so::analysis
