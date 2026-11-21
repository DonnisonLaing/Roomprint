#pragma once

#include "Common.h"

namespace rp
{

// "Mic / cab" colouration: a voicing curve built from a few filters, plus
// user low/high cut, resonance, proximity and an optional short comb filter
// (the hollow, placed sound of a mic that is not pointing straight at the source).
struct Voicing
{
    float peakHz, peakDb, peakQ;   // broad body / presence move
    float lsHz, lsDb;              // low shelf
    float hsHz, hsDb;              // high shelf
    float resHz, resQ;             // where the Resonance knob acts
    float lpHz;                    // fixed speaker / mic roll-off (0 = none)
    float combMs, combDepth;       // mic-offset comb (depth 0 = none)
};

constexpr int kNumVoicings = 5;

inline const Voicing& getVoicing (int idx)
{
    static const Voicing table[kNumVoicings] = {
        // Close
        { 3500.f, +2.0f, 0.9f,   100.f,  0.0f,  8000.f,  0.0f,  3500.f, 1.0f,     0.f, 0.0f, 0.00f },
        // Ribbon
        { 1200.f, -1.5f, 0.7f,   180.f, +2.5f,  5000.f, -4.0f,  2200.f, 0.8f, 11000.f, 0.7f, 0.12f },
        // Room mic
        {  700.f, -2.5f, 0.8f,   120.f, +1.0f,  6000.f, -2.0f,  1800.f, 0.7f, 14000.f, 1.3f, 0.18f },
        // Radio / lo-fi
        { 1600.f, +4.0f, 1.1f,   300.f, -3.0f,  3500.f, -2.0f,  2500.f, 1.2f,  5000.f, 0.0f, 0.00f },
        // Cab
        { 2500.f, +4.0f, 1.3f,   110.f, +3.0f,  4500.f, -3.0f,  2800.f, 1.5f,  6000.f, 0.4f, 0.10f },
    };
    return table[clampi (idx, 0, kNumVoicings - 1)];
}

class SourceStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        comb.prepare ((int) (0.004 * sr) + 8);
        lastVoicing = -1;
        lastRes = lastProx = lastLow = lastHigh = -1.0e9f;
        reset();
    }

    void reset()
    {
        hp.reset(); lp.reset(); peakF.reset(); ls.reset(); hs.reset();
        res.reset(); prox.reset(); voiceLP.reset(); comb.clear();
    }

    void update (int voicing, float lowCutHz, float highCutHz, float resonance, float proximity)
    {
        const Voicing& v = getVoicing (voicing);

        if (voicing != lastVoicing)
        {
            lastVoicing = voicing;
            peakF.peak (sampleRate, v.peakHz, v.peakQ, v.peakDb);
            ls.lowShelf (sampleRate, v.lsHz, v.lsDb);
            hs.highShelf (sampleRate, v.hsHz, v.hsDb);
            useVoiceLP = v.lpHz > 0.0f && v.lpHz < 0.45f * (float) sampleRate;
            if (useVoiceLP) voiceLP.lowpass (sampleRate, v.lpHz);
            combDepth = v.combDepth;
            combSamples = std::max (1.0f, v.combMs * 0.001f * (float) sampleRate);
            comb.clear();
            lastRes = -1.0e9f;   // the resonance frequency belongs to the voicing
        }

        if (resonance != lastRes)
        {
            lastRes = resonance;
            res.peak (sampleRate, v.resHz, v.resQ, resonance * 8.0f);
        }

        if (proximity != lastProx)
        {
            lastProx = proximity;
            prox.lowShelf (sampleRate, 140.0, proximity * 6.0);
        }

        if (lowCutHz != lastLow)
        {
            lastLow = lowCutHz;
            useHP = lowCutHz > 20.5f;
            if (useHP) hp.highpass (sampleRate, lowCutHz);
        }

        if (highCutHz != lastHigh)
        {
            lastHigh = highCutHz;
            useLP = highCutHz < 19000.0f && highCutHz < 0.45f * (float) sampleRate;
            if (useLP) lp.lowpass (sampleRate, highCutHz);
        }
    }

    inline float process (float x)
    {
        float y = x;
        if (useHP) y = hp.process (y);
        y = ls.process (y);
        y = peakF.process (y);
        y = hs.process (y);
        y = res.process (y);
        y = prox.process (y);
        if (useVoiceLP) y = voiceLP.process (y);
        if (useLP) y = lp.process (y);

        if (combDepth > 0.0f)
        {
            const float d = comb.read (combSamples);
            comb.push (y);
            y += combDepth * d;
        }
        return y;
    }

private:
    double sampleRate = 48000.0;
    Biquad hp, lp, peakF, ls, hs, res, prox, voiceLP;
    DelayLine comb;
    bool useHP = false, useLP = false, useVoiceLP = false;
    float combDepth = 0.0f, combSamples = 1.0f;
    int lastVoicing = -1;
    float lastRes = -1.0e9f, lastProx = -1.0e9f, lastLow = -1.0e9f, lastHigh = -1.0e9f;
};

} // namespace rp
