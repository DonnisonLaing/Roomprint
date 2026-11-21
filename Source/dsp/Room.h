#pragma once

#include "Common.h"
#include <array>

namespace rp
{

//==============================================================================
struct Material
{
    float beta;      // broadband wall reflection coefficient
    float hfMul;     // high-frequency RT60 as a fraction of the mid RT60
    float erLpHz;    // low-pass for first-order reflections (higher orders go darker)
};

inline const Material& getMaterial (int idx)
{
    static const Material table[4] = {
        { 0.78f, 0.55f,  7000.f },   // Wood
        { 0.92f, 0.85f, 12000.f },   // Concrete
        { 0.55f, 0.30f,  4000.f },   // Padded
        { 0.90f, 0.95f, 16000.f },   // Tiled
    };
    return table[clampi (idx, 0, 3)];
}

//==============================================================================
// Early reflections from a shoebox room (image-source method, up to 3rd order).
// One mono source, two listeners (stereo mic pair). Taps are grouped by
// reflection order; each group gets its own low-pass so later reflections are
// darker. Tap delays and gains are ramped, so moving Size sounds like a room
// changing shape rather than clicking.
class EarlyReflections
{
public:
    static constexpr int kMaxTaps = 64;
    static constexpr float kMicSpacing = 0.30f;   // metres
    static constexpr float kOutScale = 1.82f;     // brings unit-RMS noise in to ~unit RMS out (measured)

    void prepare (double sr)
    {
        sampleRate = sr;
        const int maxSamples = (int) (0.42 * sr);
        line.prepare (maxSamples);
        maxDelay = (float) maxSamples;
        reset();
    }

    void reset()
    {
        line.clear();
        for (auto& ch : lp) for (auto& f : ch) f.reset();
        for (auto& ch : taps) for (auto& t : ch) t = Tap {};
        numTaps = 0;
        haveGeometry = false;
    }

    int getNumTaps() const { return numTaps; }

    void setGeometry (float sizeScale, float beta, float lpHz)
    {
        if (haveGeometry && sizeScale == lastSize && beta == lastBeta && lpHz == lastLp)
        {
            for (auto& ch : taps) for (int k = 0; k < numTaps; ++k) { ch[(size_t) k].dInc = 0.0f; ch[(size_t) k].gInc = 0.0f; }
            return;
        }

        const bool snap = ! haveGeometry;
        haveGeometry = true;
        lastSize = sizeScale; lastBeta = beta; lastLp = lpHz;

        const float sr = (float) sampleRate;
        const float Lx = 5.0f * sizeScale, Ly = 4.0f * sizeScale, Lz = 2.8f * sizeScale;
        const float sx = 0.25f * Lx, sy = 0.42f * Ly, sz = 0.38f * Lz;           // source
        const float lx = 0.62f * Lx, lz = 0.45f * Lz;                             // listeners
        const float lyc[2] = { 0.5f * Ly - 0.5f * kMicSpacing, 0.5f * Ly + 0.5f * kMicSpacing };

        for (int c = 0; c < 2; ++c)
        {
            float dT[kMaxTaps], gT[kMaxTaps];
            int grp[kMaxTaps];
            int n = 0;
            double energy = 0.0;

            for (int qx = 0; qx < 2; ++qx)
            for (int qy = 0; qy < 2; ++qy)
            for (int qz = 0; qz < 2; ++qz)
            for (int j = -3; j <= 3; ++j)
            for (int k = -3; k <= 3; ++k)
            for (int m = -3; m <= 3; ++m)
            {
                const int order = std::abs (j - qx) + std::abs (j) + std::abs (k - qy) + std::abs (k)
                                + std::abs (m - qz) + std::abs (m);
                if (order < 1 || order > 3 || n >= kMaxTaps) continue;

                const float ix = (float) (1 - 2 * qx) * sx + 2.0f * (float) j * Lx - lx;
                const float iy = (float) (1 - 2 * qy) * sy + 2.0f * (float) k * Ly - lyc[c];
                const float iz = (float) (1 - 2 * qz) * sz + 2.0f * (float) m * Lz - lz;
                const float r = std::max (0.3f, std::sqrt (ix * ix + iy * iy + iz * iz));

                dT[n] = clampf (r / 343.0f * sr, 1.0f, maxDelay - 4.0f);
                gT[n] = std::pow (beta, (float) order) / r;
                grp[n] = order - 1;
                energy += (double) gT[n] * (double) gT[n];
                ++n;
            }

            numTaps = n;
            const float norm = energy > 0.0 ? (float) (1.0 / std::sqrt (energy)) : 0.0f;

            for (int i = 0; i < n; ++i)
            {
                Tap& t = taps[(size_t) c][(size_t) i];
                t.group = grp[i];
                if (snap) { t.d = dT[i]; t.g = gT[i] * norm; t.dInc = 0.0f; t.gInc = 0.0f; }
                else
                {
                    t.dInc = (dT[i] - t.d) / (float) kControlBlock;
                    t.gInc = (gT[i] * norm - t.g) / (float) kControlBlock;
                }
            }
        }

        const double fc[3] = { (double) lpHz, (double) lpHz * 0.6, (double) lpHz * 0.4 };
        for (int c = 0; c < 2; ++c)
            for (int g = 0; g < 3; ++g)
                lp[(size_t) c][(size_t) g].setCutoff (sampleRate, std::max (800.0, fc[g]));
    }

    inline void process (float in, float& outL, float& outR)
    {
        float acc[2][3] = {};
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < numTaps; ++k)
            {
                Tap& t = taps[(size_t) c][(size_t) k];
                t.d += t.dInc;
                t.g += t.gInc;
                acc[c][t.group] += t.g * line.read (t.d);
            }
        }
        line.push (in);

        outL = kOutScale * (lp[0][0].process (acc[0][0]) + lp[0][1].process (acc[0][1]) + lp[0][2].process (acc[0][2]));
        outR = kOutScale * (lp[1][0].process (acc[1][0]) + lp[1][1].process (acc[1][1]) + lp[1][2].process (acc[1][2]));
    }

private:
    struct Tap { float d = 1.0f, dInc = 0.0f, g = 0.0f, gInc = 0.0f; int group = 0; };

    double sampleRate = 48000.0;
    DelayLine line;
    float maxDelay = 1000.0f;
    std::array<std::array<Tap, kMaxTaps>, 2> taps {};
    std::array<std::array<OnePoleLP, 3>, 2> lp {};
    int numTaps = 0;
    bool haveGeometry = false;
    float lastSize = 0.0f, lastBeta = 0.0f, lastLp = 0.0f;
};

//==============================================================================
// Late reverb: 8-line feedback delay network, Householder feedback matrix,
// slowly modulated delay lengths, one-pole frequency-dependent decay, and
// input allpass diffusion. Stereo in, stereo out (lines are interleaved
// between the channels so the tail is decorrelated).
class LateFDN
{
public:
    static constexpr int N = 8;

    // Output scale that makes a unit-RMS white noise input come out at roughly unit
    // RMS for a 0.5 s decay (measured by the test suite).
    static constexpr float kOutScale = 4.46f;

    void prepare (double sr)
    {
        sampleRate = sr;
        const float srScale = (float) (sr / 48000.0);
        for (int i = 0; i < N; ++i)
        {
            const int maxLen = (int) (0.0677 * 3.3 * sr + 64.0 + 4.0 * srScale);
            line[(size_t) i].prepare (maxLen);
            maxLenSamples[(size_t) i] = (float) maxLen;
            modRate[(size_t) i] = kModHz[i] / (float) sr;
            modDepth[(size_t) i] = (0.8f + 0.2f * (float) i) * srScale;
        }

        static const float apL[4] = { 4.77f, 6.31f, 8.93f, 12.18f };
        static const float apR[4] = { 5.13f, 7.01f, 9.61f, 13.33f };
        for (int k = 0; k < 4; ++k)
        {
            apLeft[(size_t) k].prepare ((int) std::ceil (apL[k] * 0.001 * sr));
            apRight[(size_t) k].prepare ((int) std::ceil (apR[k] * 0.001 * sr));
        }
        reset();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i)
        {
            line[(size_t) i].clear();
            damp[(size_t) i] = 0.0f;
            modPhase[(size_t) i] = (float) i / (float) N;
        }
        for (auto& a : apLeft) a.clear();
        for (auto& a : apRight) a.clear();
        initialised = false;
    }

    void setParams (float sizeScale, float rt60, float hfMul, float diffusion)
    {
        const float sr = (float) sampleRate;
        const double refHz = std::min (6000.0, 0.45 * sampleRate);
        const double cosRef = std::cos (2.0 * kPi * refHz / sampleRate);
        rt60 = std::max (rt60, 0.05f);
        const float rtHf = rt60 * clampf (hfMul, 0.05f, 1.0f);

        for (int i = 0; i < N; ++i)
        {
            const float centre = kBaseMs[i] * 0.001f * sizeScale * sr;

            modPhase[(size_t) i] += modRate[(size_t) i] * (float) kControlBlock;
            modPhase[(size_t) i] -= std::floor (modPhase[(size_t) i]);
            const float target = clampf (centre + modDepth[(size_t) i] * std::sin (2.0f * (float) kPi * modPhase[(size_t) i]),
                                         8.0f, maxLenSamples[(size_t) i] - 4.0f);

            if (! initialised) dCur[(size_t) i] = target;
            dInc[(size_t) i] = (target - dCur[(size_t) i]) / (float) kControlBlock;

            const float tau = centre / sr;
            const float gMid = std::pow (10.0f, -3.0f * tau / rt60);
            const float gHf  = std::pow (10.0f, -3.0f * tau / rtHf);
            g[(size_t) i] = gMid;
            p[(size_t) i] = dampCoefficient (gHf / gMid, cosRef);
        }

        const float apG = 0.35f + 0.4f * clampf (diffusion, 0.0f, 1.0f);
        for (auto& a : apLeft) a.g = apG;
        for (auto& a : apRight) a.g = apG;

        outScale = kOutScale * std::sqrt (0.5f / rt60);
        initialised = true;
    }

    inline void process (float inL, float inR, float& outL, float& outR)
    {
        float xl = inL, xr = inR;
        for (auto& a : apLeft) xl = a.process (xl);
        for (auto& a : apRight) xr = a.process (xr);

        float y[N];
        float sum = 0.0f, oL = 0.0f, oR = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const size_t s = (size_t) i;
            dCur[s] += dInc[s];
            const float raw = line[s].read (dCur[s]);

            damp[s] = flushDenormal ((1.0f - p[s]) * raw + p[s] * damp[s]);
            const float o = g[s] * damp[s];
            y[i] = o;
            sum += o;

            oL += kSign[i] * kCL[i] * o;
            oR += kSign[i] * kCR[i] * o;
        }

        const float k = sum * (2.0f / (float) N);   // Householder reflection
        for (int i = 0; i < N; ++i)
        {
            const size_t s = (size_t) i;
            const float inj = kSign[i] * (xl * kCL[i] + xr * kCR[i]) * kInject;
            line[s].push (y[i] - k + inj);
        }

        outL = oL * kOutNorm * outScale;
        outR = oR * kOutNorm * outScale;
    }

private:
    // Coefficient p of H(z) = (1-p) / (1 - p z^-1) such that |H| at the reference
    // frequency equals the requested ratio (decay at HF relative to mid).
    static float dampCoefficient (float ratio, double cosRef)
    {
        if (ratio >= 0.9999f) return 0.0f;
        const double r2 = (double) ratio * (double) ratio;
        const double A = 1.0 - r2 * cosRef;
        const double disc = std::max (0.0, A * A - (1.0 - r2) * (1.0 - r2));
        const double pp = (A - std::sqrt (disc)) / (1.0 - r2);
        return (float) std::min (std::max (pp, 0.0), 0.98);
    }

    static constexpr float kBaseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 50.9f, 57.1f, 61.9f, 67.7f };
    static constexpr float kModHz[N]  = { 0.11f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.61f };
    static constexpr float kSign[N]   = { 1, 1, -1, -1, 1, 1, -1, -1 };
    static constexpr float kCL[N]     = { 1.0f, 0.4f, 1.0f, 0.4f, 1.0f, 0.4f, 1.0f, 0.4f };
    static constexpr float kCR[N]     = { 0.4f, 1.0f, 0.4f, 1.0f, 0.4f, 1.0f, 0.4f, 1.0f };
    static constexpr float kInject    = 0.5f;
    static constexpr float kOutNorm   = 0.4642f;   // 1 / sqrt (4 * (1 + 0.4^2))

    double sampleRate = 48000.0;
    std::array<DelayLine, N> line;
    std::array<float, N> maxLenSamples {}, modRate {}, modDepth {}, modPhase {};
    std::array<float, N> dCur {}, dInc {}, g {}, p {}, damp {};
    std::array<Allpass, 4> apLeft, apRight;
    float outScale = 1.0f;
    bool initialised = false;
};

} // namespace rp
