#pragma once

// Framework-independent DSP helpers for Roomprint. No JUCE dependency, so the
// whole engine can be compiled and tested on its own.

#include <algorithm>
#include <cmath>
#include <vector>

namespace rp
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kControlBlock = 32;   // derived parameters are refreshed every 32 samples

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi (int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf (float a, float b, float t)    { return a + (b - a) * t; }
inline float dbToGain (float db)                  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)                   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float flushDenormal (float x)              { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }

inline float smoothstep (float e0, float e1, float x)
{
    const float t = clampf ((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Pade tanh approximation; clamped at +-3 where its slope is already zero (C1 continuous).
inline float fastTanh (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
// Control-rate exponential smoother (advance once per control block).
struct Smoother
{
    float current = 0.0f, target = 0.0f, coeff = 1.0f;

    void prepare (double controlRateHz, float timeMs)
    {
        coeff = 1.0f - (float) std::exp (-1.0 / (controlRateHz * 0.001 * (double) timeMs));
    }
    void set (float t)       { target = t; }
    void snapToTarget()      { current = target; }
    void reset (float v)     { current = target = v; }
    float next()
    {
        current += coeff * (target - current);
        if (std::fabs (target - current) < 1.0e-6f * (1.0f + std::fabs (target)))
            current = target;
        return current;
    }
};

// Linear per-sample ramp towards a target reached after n samples.
struct Ramp
{
    float cur = 0.0f, inc = 0.0f, target = 0.0f;

    void snap (float v)                              { cur = target = v; inc = 0.0f; }
    void setTarget (float t, int n = kControlBlock)  { target = t; inc = (t - cur) / (float) n; }
    float next()                                     { cur += inc; return cur; }
};

//==============================================================================
// RBJ biquad, transposed direct form II, double precision state.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset()       { z1 = z2 = 0.0; }
    void setIdentity() { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }
    void copyCoefficients (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    inline float process (float xf)
    {
        const double x = (double) xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void lowpass (double sr, double f, double q = 0.70710678)
    {
        double cs, sn, alpha; trig (sr, f, q, cs, sn, alpha);
        setRaw ((1 - cs) * 0.5, 1 - cs, (1 - cs) * 0.5, 1 + alpha, -2 * cs, 1 - alpha);
    }

    void highpass (double sr, double f, double q = 0.70710678)
    {
        double cs, sn, alpha; trig (sr, f, q, cs, sn, alpha);
        setRaw ((1 + cs) * 0.5, -(1 + cs), (1 + cs) * 0.5, 1 + alpha, -2 * cs, 1 - alpha);
    }

    void peak (double sr, double f, double q, double gainDb)
    {
        double cs, sn, alpha; trig (sr, f, q, cs, sn, alpha);
        const double A = std::pow (10.0, gainDb / 40.0);
        setRaw (1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A);
    }

    void lowShelf (double sr, double f, double gainDb)
    {
        double cs, sn, alpha; trig (sr, f, 1.0, cs, sn, alpha);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double beta = 2.0 * std::sqrt (A) * (sn * 0.5 * std::sqrt (2.0));
        setRaw (A * ((A + 1) - (A - 1) * cs + beta),
                2 * A * ((A - 1) - (A + 1) * cs),
                A * ((A + 1) - (A - 1) * cs - beta),
                (A + 1) + (A - 1) * cs + beta,
                -2 * ((A - 1) + (A + 1) * cs),
                (A + 1) + (A - 1) * cs - beta);
    }

    void highShelf (double sr, double f, double gainDb)
    {
        double cs, sn, alpha; trig (sr, f, 1.0, cs, sn, alpha);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double beta = 2.0 * std::sqrt (A) * (sn * 0.5 * std::sqrt (2.0));
        setRaw (A * ((A + 1) + (A - 1) * cs + beta),
                -2 * A * ((A - 1) + (A + 1) * cs),
                A * ((A + 1) + (A - 1) * cs - beta),
                (A + 1) - (A - 1) * cs + beta,
                2 * ((A - 1) - (A + 1) * cs),
                (A + 1) - (A - 1) * cs - beta);
    }

    // Magnitude response, for tests.
    double magnitudeAt (double sr, double f) const
    {
        const double w = 2.0 * kPi * f / sr;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2,  di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    static void trig (double sr, double f, double q, double& cs, double& sn, double& alpha)
    {
        f = std::min (std::max (f, 5.0), sr * 0.45);
        const double w0 = 2.0 * kPi * f / sr;
        cs = std::cos (w0);
        sn = std::sin (w0);
        alpha = sn / (2.0 * q);
    }

    void setRaw (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        const double inv = 1.0 / a0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }
};

struct OnePoleLP
{
    float a = 1.0f, z = 0.0f;

    void setCutoff (double sr, double f)
    {
        f = std::min (std::max (f, 1.0), sr * 0.45);
        a = (float) (1.0 - std::exp (-2.0 * kPi * f / sr));
    }
    void reset() { z = 0.0f; }
    inline float process (float x) { z += a * (x - z); return z; }
};

struct DcBlocker
{
    float r = 0.995f, x1 = 0.0f, y1 = 0.0f;

    void setCutoff (double sr, double f) { r = (float) std::exp (-2.0 * kPi * f / sr); }
    void reset() { x1 = y1 = 0.0f; }
    inline float process (float x)
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = flushDenormal (y);
        return y;
    }
};

//==============================================================================
// Circular delay line with linear interpolation.
// Convention: call read() BEFORE push() for the current sample. read (d) then
// returns the input from d samples ago (d >= 1).
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int n = 8;
        while (n < maxDelaySamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        maxD = (float) (n - 4);
        pos = 0;
    }

    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    inline float read (float d) const
    {
        d = d < 1.0f ? 1.0f : (d > maxD ? maxD : d);
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = buf[(size_t) ((pos - i) & mask)];
        const float b = buf[(size_t) ((pos - i - 1) & mask)];
        return a + f * (b - a);
    }

    inline float readInt (int d) const { return buf[(size_t) ((pos - d) & mask)]; }

    inline void push (float x)
    {
        buf[(size_t) pos] = x;
        pos = (pos + 1) & mask;
    }

private:
    std::vector<float> buf;
    int mask = 7, pos = 0;
    float maxD = 4.0f;
};

// Schroeder allpass with an integer delay.
struct Allpass
{
    std::vector<float> buf;
    int pos = 0, n = 1;
    float g = 0.5f;

    void prepare (int length)
    {
        n = std::max (1, length);
        buf.assign ((size_t) n, 0.0f);
        pos = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

    inline float process (float x)
    {
        const float z = buf[(size_t) pos];
        const float w = x - g * z;
        buf[(size_t) pos] = flushDenormal (w);
        if (++pos >= n) pos = 0;
        return z + g * w;
    }
};

} // namespace rp
