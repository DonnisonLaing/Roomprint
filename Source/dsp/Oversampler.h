#pragma once

#include "Common.h"
#include <array>

namespace rp
{

// Linear-phase half-band FIR stage (49 taps, Kaiser window). Converts one
// sample at rate r into two samples at 2r (up) and back (down).
// Group delay at the high rate is 24 samples for each of up and down.
class HalfbandStage
{
public:
    static constexpr int kTaps = 49;
    static constexpr int kCentre = (kTaps - 1) / 2;   // 24

    HalfbandStage() { design(); reset(); }

    void reset()
    {
        uh.fill (0.0f);
        dh.fill (0.0f);
        upPos = 0;
        dnPos = 0;
    }

    // One input sample -> two output samples (time order out[0], out[1]).
    inline void up (float x, float* out)
    {
        upPos = (upPos - 1) & 31;
        uh[(size_t) upPos] = x;
        uh[(size_t) upPos + 32] = x;

        float e = 0.0f, o = 0.0f;
        for (int i = 0; i < (int) he.size(); ++i) e += he[(size_t) i] * uh[(size_t) (upPos + i)];
        for (int i = 0; i < (int) ho.size(); ++i) o += ho[(size_t) i] * uh[(size_t) (upPos + i)];
        out[0] = 2.0f * e;
        out[1] = 2.0f * o;
    }

    // Two input samples (time order a, b) -> one output sample.
    inline float down (float a, float b)
    {
        push (a);
        float y = 0.0f;
        for (int j = 0; j < kTaps; ++j) y += h[(size_t) j] * dh[(size_t) (dnPos + j)];
        push (b);
        return y;
    }

    const std::array<float, kTaps>& taps() const { return h; }

private:
    inline void push (float v)
    {
        dnPos = (dnPos - 1) & 63;
        dh[(size_t) dnPos] = v;
        dh[(size_t) dnPos + 64] = v;
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k)
        {
            const double t = x / (2.0 * k);
            term *= t * t;
            sum += term;
        }
        return sum;
    }

    void design()
    {
        const double beta = 7.5;
        double sum = 0.0;
        std::array<double, kTaps> d {};
        for (int j = 0; j < kTaps; ++j)
        {
            const int k = j - kCentre;
            double v = (k == 0) ? 0.5 : std::sin (kPi * k * 0.5) / (kPi * k);
            const double r = (double) k / (double) kCentre;
            v *= besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            d[(size_t) j] = v;
            sum += v;
        }
        for (int j = 0; j < kTaps; ++j) h[(size_t) j] = (float) (d[(size_t) j] / sum);

        for (size_t i = 0; i < he.size(); ++i) he[i] = h[2 * i];
        for (size_t i = 0; i < ho.size(); ++i) ho[i] = h[2 * i + 1];
    }

    std::array<float, kTaps> h {};
    std::array<float, (kTaps + 1) / 2> he {};   // even taps (25)
    std::array<float, (kTaps - 1) / 2> ho {};   // odd taps  (24)
    std::array<float, 64> uh {};
    std::array<float, 128> dh {};
    int upPos = 0, dnPos = 0;
};

//==============================================================================
// 1x / 2x / 4x oversampler around a per-sample nonlinear function.
class Oversampler
{
public:
    void setFactor (int f)
    {
        factor = (f >= 4) ? 4 : (f >= 2 ? 2 : 1);
        reset();
    }
    int getFactor() const { return factor; }

    // Latency in base-rate samples.
    int getLatency() const
    {
        const int l = HalfbandStage::kCentre;   // 24 for the outer stage
        return factor == 1 ? 0 : (factor == 2 ? l : l + l / 2);
    }

    void reset() { s1.reset(); s2.reset(); }

    template <class F>
    inline float process (float x, F&& f)
    {
        if (factor == 1)
            return f (x);

        float u[2];
        s1.up (x, u);

        if (factor == 2)
            return s1.down (f (u[0]), f (u[1]));

        float r[2];
        for (int i = 0; i < 2; ++i)
        {
            float w[2];
            s2.up (u[i], w);
            r[i] = s2.down (f (w[0]), f (w[1]));
        }
        return s1.down (r[0], r[1]);
    }

private:
    int factor = 1;
    HalfbandStage s1, s2;
};

} // namespace rp
