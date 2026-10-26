#pragma once

// Radix-2 complex FFT (iterative, in place). Headless, no external libraries.

#include <complex>
#include <vector>

namespace so::analysis
{

class FFT
{
public:
    using Complex = std::complex<float>;

    /** Size is 2^order, order in 1..20. Throws std::invalid_argument otherwise. */
    explicit FFT (int order);

    int size() const noexcept { return n; }
    int getOrder() const noexcept { return order; }

    /** Forward transform, X[k] = sum x[n] e^(-2 pi i k n / N). `data` holds size() values. */
    void forward (Complex* data) const noexcept;

    /** Inverse transform, scaled by 1/N so inverse(forward(x)) == x. */
    void inverse (Complex* data) const noexcept;

    /** Magnitudes of bins 0..N/2 of a real frame (`in` holds size() samples, `out` size()/2 + 1).
        `scratch` is resized as needed so callers can reuse it without reallocating. */
    void realMagnitudes (const float* in, float* out, std::vector<Complex>& scratch) const;

    /** Smallest order whose size is >= n. */
    static int orderFor (int minSize) noexcept;

private:
    void transform (Complex* data, bool inverse) const noexcept;

    int order;
    int n;
    std::vector<Complex> twiddles;   // e^(-2 pi i k / N), k < N/2
    std::vector<unsigned> bitReverse;
};

} // namespace so::analysis
