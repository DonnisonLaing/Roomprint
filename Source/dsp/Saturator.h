#pragma once

#include "Common.h"
#include "Oversampler.h"

namespace rp
{

// Tape / tube / transformer style saturation (one channel).
//
//   input -> pre-emphasis -> [oversampled asymmetric tanh] -> DC block
//         -> de-emphasis -> head bump -> HF loss -> tone -> blend with dry
//
// The dry branch of the blend is delayed to match the oversampler latency, so
// the Mix control never combs.
class Saturator
{
public:
    enum Type { Tape = 0, Tube = 1, Transformer = 2 };

    void prepare (double sr)
    {
        sampleRate = sr;
        dryDelay.prepare (64);
        sagCoeff = 1.0f - (float) std::exp (-1.0 / (0.03 * sr));
        dc.setCutoff (sr, 8.0);
        lastType = -1;
        lastTone = 1.0e9f;
        reset();
    }

    void reset()
    {
        pre.reset(); post.reset(); bump.reset(); tone.reset();
        loss.reset(); dc.reset(); os.reset(); dryDelay.clear();
        env = 0.0f;
    }

    void setOversampling (int factor)
    {
        os.setFactor (factor);
        dryDelay.clear();
    }

    int getLatency() const { return os.getLatency(); }

    void setParams (int type, float driveDb, float bias01, float toneDb, float mix)
    {
        if (type != lastType)
        {
            lastType = type;
            switch (type)
            {
                case Tape:         // HF is pushed harder into the curve, then restored: bright "tape squash"
                    pre.highShelf (sampleRate, 3000.0, 6.0);
                    post.highShelf (sampleRate, 3000.0, -6.0);
                    bump.peak (sampleRate, 70.0, 0.9, 2.0);
                    loss.setCutoff (sampleRate, 16000.0);
                    sagAmount = 0.0f;
                    break;
                case Tube:         // even harmonics from the bias, gain sag under hot signals
                    pre.setIdentity();
                    post.setIdentity();
                    bump.peak (sampleRate, 120.0, 0.7, 1.0);
                    loss.setCutoff (sampleRate, 20000.0);
                    sagAmount = 0.35f;
                    break;
                default:           // transformer: low end is driven harder
                    pre.lowShelf (sampleRate, 140.0, 9.0);
                    post.lowShelf (sampleRate, 140.0, -9.0);
                    bump.peak (sampleRate, 55.0, 0.8, 1.5);
                    loss.setCutoff (sampleRate, 18000.0);
                    sagAmount = 0.0f;
                    break;
            }
        }

        if (toneDb != lastTone)
        {
            lastTone = toneDb;
            tone.highShelf (sampleRate, 3000.0, toneDb);
        }

        gain = dbToGain (driveDb);
        comp = 1.0f / std::sqrt (gain);
        biasOffset = bias01 * 0.25f;
        blend = mix;
    }

    inline float process (float x)
    {
        const int lat = os.getLatency();
        const float dry = lat > 0 ? dryDelay.readInt (lat) : x;
        dryDelay.push (x);

        const float s = pre.process (x);
        env += sagCoeff * (std::fabs (s) - env);
        const float gEff = gain / (1.0f + sagAmount * env * gain);
        const float off = biasOffset;
        const float tOff = fastTanh (gEff * off);

        float y = os.process (s, [gEff, off, tOff] (float v) { return fastTanh (gEff * (v + off)) - tOff; });
        y *= comp;
        y = dc.process (y);
        y = post.process (y);
        y = bump.process (y);
        y = loss.process (y);
        y = tone.process (y);

        return dry + blend * (y - dry);
    }

private:
    double sampleRate = 48000.0;
    Oversampler os;
    DelayLine dryDelay;
    Biquad pre, post, bump, tone;
    OnePoleLP loss;
    DcBlocker dc;
    float env = 0.0f, sagCoeff = 0.001f, sagAmount = 0.0f;
    float gain = 1.0f, comp = 1.0f, biasOffset = 0.0f, blend = 1.0f;
    int lastType = -1;
    float lastTone = 1.0e9f;
};

} // namespace rp
