#pragma once

#include "Common.h"
#include "Oversampler.h"
#include "Room.h"
#include "Saturator.h"
#include "SourceStage.h"
#include <array>

namespace rp
{

//==============================================================================
// Everything the plugin exposes, in engine units.
struct Params
{
    // Distance macro
    float distance = 0.40f;        // 0..1
    float distDepth = 1.0f;        // 0..1, how far the macro can reach

    // Room
    float size = 0.45f;            // 0..1
    float decaySec = 0.5f;         // RT60 of the late tail, mid band
    float diffusion = 0.7f;        // 0..1
    float width = 1.0f;            // 0..1.5, side level of the room return
    float roomLowCutHz = 80.0f;
    float roomHighCutHz = 12000.0f;
    int   material = 0;            // 0 wood, 1 concrete, 2 padded, 3 tiled

    // Source (mic / cab)
    int   voicing = 0;             // 0 close, 1 ribbon, 2 room mic, 3 radio, 4 cab
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    float resonance = 0.3f;        // 0..1
    float proximity = 0.3f;        // 0..1

    // Saturation
    int   satType = 0;             // 0 tape, 1 tube, 2 transformer
    float drive = 0.3f;            // 0..1
    float bias = 0.0f;             // 0..1
    float tone = 0.0f;             // -1..1
    float satMix = 1.0f;           // 0..1
    bool  satPost = false;         // false: before the room, true: after
    int   oversampling = 1;        // 0 off, 1 = 2x, 2 = 4x

    // Glue
    float squash = 0.0f;           // 0..1, compression on the room return
    float duck = 0.0f;             // 0..1, room ducks under the input

    // Output
    float mix = 1.0f;              // 0..1
    float outputDb = 0.0f;
};

//==============================================================================
// One macro (distance, 0..1) -> the balance of direct sound, early
// reflections and late tail, plus pre-delay and air absorption. This is the
// "Smart Reverb distance" idea in one place so it can be tuned and tested.
struct DistanceMap
{
    float directDb;
    float erDb;
    float lateDb;
    float predelayMs;
    float airHz;
    float proximityScale;
};

inline DistanceMap mapDistance (float dd)
{
    dd = clampf (dd, 0.0f, 1.0f);
    DistanceMap m {};
    m.directDb   = -22.0f * std::pow (dd, 1.2f);
    m.erDb       = lerpf (-26.0f, -9.0f, smoothstep (0.0f, 0.6f, dd)) - 5.0f * smoothstep (0.6f, 1.0f, dd);
    m.lateDb     = lerpf (-26.0f, -10.0f, std::pow (dd, 0.7f));
    m.predelayMs = 1.0f + 28.0f * dd;
    m.airHz      = 20000.0f * std::pow (0.2f, dd);
    m.proximityScale = 1.0f - 0.9f * dd;
    return m;
}

//==============================================================================
class Engine
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        const double controlRate = sr / (double) kControlBlock;

        const float times[kNumSmoothed] = {
            60, 60, 150, 150, 80, 60, 40, 40, 40, 40, 60, 60, 60, 60, 60, 60, 60, 60, 40, 40 };
        for (int i = 0; i < kNumSmoothed; ++i)
            sm[(size_t) i].prepare (controlRate, times[i]);

        for (size_t c = 0; c < 2; ++c)
        {
            src[c].prepare (sr);
            sat[c].prepare (sr);
            predelay[c].prepare ((int) (0.07 * sr) + 8);
            dryDelay[c].prepare (64);
        }
        er.prepare (sr);
        fdn.prepare (sr);
        maxPredelay = (float) (0.06 * sr);

        duckAttack  = 1.0f - (float) std::exp (-1.0 / (0.001 * sr));
        duckRelease = 1.0f - (float) std::exp (-1.0 / (0.120 * sr));
        compAttack  = 1.0f - (float) std::exp (-1.0 / (0.002 * sr));
        compRelease = 1.0f - (float) std::exp (-1.0 / (0.090 * sr));

        osChoice = -1;
        needSnap = true;
        setParams (Params {});
        reset();
    }

    void reset()
    {
        for (size_t c = 0; c < 2; ++c)
        {
            src[c].reset();
            sat[c].reset();
            predelay[c].clear();
            dryDelay[c].clear();
            airLP[c].reset(); roomHP[c].reset(); roomLP[c].reset();
        }
        er.reset();
        fdn.reset();
        duckEnv = 0.0f;
        compGr = 0.0f;
        ctrlCounter = 0;
        firstControl = true;
        needSnap = true;   // the next setParams() jumps straight to its values instead of gliding from old ones
    }

    void setParams (const Params& p)
    {
        sm[kDistance].set (p.distance);   sm[kDepth].set (p.distDepth);
        sm[kSize].set (p.size);           sm[kDecay].set (p.decaySec);
        sm[kDiffusion].set (p.diffusion); sm[kWidth].set (p.width);
        sm[kRoomLow].set (p.roomLowCutHz); sm[kRoomHigh].set (p.roomHighCutHz);
        sm[kLowCut].set (p.lowCutHz);     sm[kHighCut].set (p.highCutHz);
        sm[kRes].set (p.resonance);       sm[kProx].set (p.proximity);
        sm[kDrive].set (p.drive);         sm[kBias].set (p.bias);
        sm[kTone].set (p.tone);           sm[kSatMix].set (p.satMix);
        sm[kSquash].set (p.squash);       sm[kDuck].set (p.duck);
        sm[kMix].set (p.mix);             sm[kOut].set (p.outputDb);

        voicing  = p.voicing;
        material = p.material;
        satType  = p.satType;
        satPost  = p.satPost;

        if (p.oversampling != osChoice)
        {
            osChoice = p.oversampling;
            const int factor = osChoice <= 0 ? 1 : (osChoice == 1 ? 2 : 4);
            for (auto& s : sat) s.setOversampling (factor);
            latency = sat[0].getLatency();
            for (auto& d : dryDelay) d.clear();
        }

        if (needSnap)
        {
            for (auto& s : sm) s.snapToTarget();
            needSnap = false;
        }
    }

    // Latency reported to the host (oversampler group delay).
    int getLatencySamples() const { return latency; }

    // In-place stereo processing. Control updates happen on a fixed 32-sample
    // grid, so the output does not depend on the host's block size.
    void process (float* left, float* right, int numSamples)
    {
        int i = 0;
        while (i < numSamples)
        {
            if (ctrlCounter == 0)
            {
                updateControl();
                ctrlCounter = kControlBlock;
            }
            const int m = std::min (numSamples - i, ctrlCounter);
            for (int k = 0; k < m; ++k)
                processSample (left[i + k], right[i + k]);
            ctrlCounter -= m;
            i += m;
        }
    }

private:
    enum
    {
        kDistance, kDepth, kSize, kDecay, kDiffusion, kWidth, kRoomLow, kRoomHigh,
        kLowCut, kHighCut, kRes, kProx, kDrive, kBias, kTone, kSatMix,
        kSquash, kDuck, kMix, kOut, kNumSmoothed
    };

    void updateControl()
    {
        for (auto& s : sm) s.next();
        auto v = [this] (int idx) { return sm[(size_t) idx].current; };
        const float sr = (float) sampleRate;

        const float dd = clampf (v (kDistance) * v (kDepth), 0.0f, 1.0f);
        const DistanceMap dm = mapDistance (dd);
        const float size = v (kSize);
        const float sizeScale = 0.35f * std::pow (9.0f, size);
        const float rt60 = std::max (0.05f, v (kDecay));
        const Material& mat = getMaterial (material);

        // --- source, saturation, air ------------------------------------------------
        const float prox = v (kProx) * dm.proximityScale;
        for (size_t c = 0; c < 2; ++c)
        {
            src[c].update (voicing, v (kLowCut), v (kHighCut), v (kRes), prox);
            sat[c].setParams (satType, v (kDrive) * 24.0f, v (kBias), v (kTone) * 6.0f, v (kSatMix));
        }
        airLP[0].lowpass (sampleRate, dm.airHz);
        airLP[1].copyCoefficients (airLP[0]);

        // --- room return filters ------------------------------------------------------
        if (v (kRoomLow) > 20.5f) roomHP[0].highpass (sampleRate, v (kRoomLow)); else roomHP[0].setIdentity();
        roomLP[0].lowpass (sampleRate, v (kRoomHigh));
        roomHP[1].copyCoefficients (roomHP[0]);
        roomLP[1].copyCoefficients (roomLP[0]);
        widthNow = v (kWidth);

        // --- room generators ---------------------------------------------------------
        er.setGeometry (sizeScale, mat.beta, mat.erLpHz);
        fdn.setParams (sizeScale, rt60, mat.hfMul, v (kDiffusion));

        const float pdTarget = clampf (dm.predelayMs * (0.6f + 0.8f * size) * 0.001f * sr, 1.0f, maxPredelay);
        if (firstControl) pdCur = pdTarget;
        pdInc = (pdTarget - pdCur) / (float) kControlBlock;

        // --- levels ------------------------------------------------------------------
        const float mix = v (kMix);
        setRamp (directGain, dbToGain (dm.directDb));
        setRamp (erGain,     dbToGain (dm.erDb));
        setRamp (lateGain,   dbToGain (dm.lateDb));
        setRamp (wetGain,    mix);
        setRamp (dryGain,    1.0f - mix);
        setRamp (outGain,    dbToGain (v (kOut)));

        // --- ducking / squash (control-rate part) ------------------------------------
        const float duckAmt = v (kDuck);
        const float envDb = gainToDb (duckEnv);
        setRamp (duckGain, dbToGain (-duckAmt * 12.0f * clampf ((envDb + 36.0f) / 24.0f, 0.0f, 1.0f)));
        squashAmt = v (kSquash);
        compThresholdDb = -30.0f * squashAmt;
        compMakeup = dbToGain (-compThresholdDb * 0.3f);

        firstControl = false;
    }

    void setRamp (Ramp& r, float target)
    {
        if (firstControl) r.snap (target);
        else r.setTarget (target);
    }

    inline void processSample (float& l, float& r)
    {
        const float inL = l, inR = r;

        // Dry path, delayed to line up with the oversampler latency.
        float dryL = inL, dryR = inR;
        if (latency > 0)
        {
            dryL = dryDelay[0].readInt (latency);
            dryR = dryDelay[1].readInt (latency);
        }
        dryDelay[0].push (inL);
        dryDelay[1].push (inR);

        // 1. source colouration (+ saturation when it sits before the room)
        float sL = src[0].process (inL);
        float sR = src[1].process (inR);
        if (! satPost)
        {
            sL = sat[0].process (sL);
            sR = sat[1].process (sR);
        }

        // 2. direct path: air absorption + distance attenuation
        const float dg = directGain.next();
        const float dL = airLP[0].process (sL) * dg;
        const float dR = airLP[1].process (sR) * dg;

        // 3. room send: pre-delay, early reflections, late tail
        pdCur += pdInc;
        const float pdL = predelay[0].read (pdCur);
        const float pdR = predelay[1].read (pdCur);
        predelay[0].push (sL);
        predelay[1].push (sR);

        float erL, erR, lateL, lateR;
        er.process (0.5f * (pdL + pdR), erL, erR);
        fdn.process (pdL, pdR, lateL, lateR);

        const float eg = erGain.next(), lg = lateGain.next();
        float rL = erL * eg + lateL * lg;
        float rR = erR * eg + lateR * lg;

        rL = roomLP[0].process (roomHP[0].process (rL));
        rR = roomLP[1].process (roomHP[1].process (rR));

        const float mid = 0.5f * (rL + rR);
        const float side = 0.5f * (rL - rR) * widthNow;
        rL = mid + side;
        rR = mid - side;

        // duck (envelope follows the input; gain is ramped at control rate)
        const float level = std::max (std::fabs (inL), std::fabs (inR));
        duckEnv += (level > duckEnv ? duckAttack : duckRelease) * (level - duckEnv);
        const float dkg = duckGain.next();
        rL *= dkg;
        rR *= dkg;

        // squash: fast compressor on the room return only
        if (squashAmt > 0.001f)
        {
            const float lvDb = gainToDb (std::max (std::fabs (rL), std::fabs (rR)));
            const float gr = std::max (0.0f, (lvDb - compThresholdDb) * 0.75f);   // 4:1
            compGr += (gr > compGr ? compAttack : compRelease) * (gr - compGr);
            const float cg = std::exp (-compGr * 0.115129f) * compMakeup;
            rL *= cg;
            rR *= cg;
        }

        // 4. sum, optional post-room saturation
        float wL = dL + rL;
        float wR = dR + rR;
        if (satPost)
        {
            wL = sat[0].process (wL);
            wR = sat[1].process (wR);
        }

        // 5. mix and output
        const float wg = wetGain.next(), dyg = dryGain.next(), og = outGain.next();
        l = (dryL * dyg + wL * wg) * og;
        r = (dryR * dyg + wR * wg) * og;
    }

    double sampleRate = 48000.0;
    std::array<Smoother, kNumSmoothed> sm;

    int voicing = 0, material = 0, satType = 0, osChoice = -1, latency = 0;
    bool satPost = false, needSnap = true, firstControl = true;
    int ctrlCounter = 0;

    std::array<SourceStage, 2> src;
    std::array<Saturator, 2> sat;
    std::array<DelayLine, 2> predelay, dryDelay;
    std::array<Biquad, 2> airLP, roomHP, roomLP;
    EarlyReflections er;
    LateFDN fdn;

    Ramp directGain, erGain, lateGain, wetGain, dryGain, outGain, duckGain;
    float pdCur = 1.0f, pdInc = 0.0f, maxPredelay = 1000.0f;
    float widthNow = 1.0f;

    float duckEnv = 0.0f, duckAttack = 0.01f, duckRelease = 0.0001f;
    float squashAmt = 0.0f, compThresholdDb = 0.0f, compMakeup = 1.0f;
    float compGr = 0.0f, compAttack = 0.01f, compRelease = 0.001f;
};

} // namespace rp
