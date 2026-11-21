// Stand-alone tests for the Roomprint DSP engine (no JUCE needed).
//   g++ -std=c++17 -O2 -I Source/dsp Tests/test_engine.cpp -o test_engine && ./test_engine

#include "Engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

#if defined(__SSE__)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

using namespace rp;

static int failures = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
    } while (0)

static const double kSr = 48000.0;

static double goertzelDb (const std::vector<float>& x, double f, double sr)
{
    const double w = 2.0 * kPi * f / sr;
    double re = 0, im = 0;
    for (size_t n = 0; n < x.size(); ++n) { re += x[n] * std::cos (w * (double) n); im += x[n] * std::sin (w * (double) n); }
    const double mag = 2.0 * std::sqrt (re * re + im * im) / (double) x.size();
    return 20.0 * std::log10 (mag + 1.0e-12);
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

static std::vector<float> whiteNoise (size_t n, float amp, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v (n);
    for (auto& s : v) s = d (rng) * amp * 1.7320508f;   // unit RMS * amp
    return v;
}

// Runs the engine over L/R inputs in blocks of 'block' samples.
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r, int block)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - i);
        e.process (l.data() + i, r.data() + i, n);
    }
}

static bool finiteAndBounded (const std::vector<float>& x, float limit)
{
    for (float s : x) if (! std::isfinite (s) || std::fabs (s) > limit) return false;
    return true;
}

//------------------------------------------------------------------------------
static void testOversampler()
{
    std::printf ("\n[oversampler]\n");

    for (int factor : { 2, 4 })
    {
        Oversampler os; os.setFactor (factor);
        std::vector<float> y (512);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = os.process (i == 0 ? 1.0f : 0.0f, [] (float v) { return v; });
        size_t peak = 0;
        for (size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i]) > std::fabs (y[peak])) peak = i;
        CHECK ((int) peak == os.getLatency() && std::fabs (y[peak] - 1.0f) < 0.1f,
               "%dx impulse peaks at reported latency %d (peak idx %zu, value %.4f)", factor, os.getLatency(), peak, y[peak]);
    }

    // passband flatness through 4x
    {
        Oversampler os; os.setFactor (4);
        for (double f : { 100.0, 1000.0, 10000.0, 16000.0 })
        {
            std::vector<float> y (8192);
            for (size_t i = 0; i < y.size(); ++i)
                y[i] = os.process ((float) std::sin (2.0 * kPi * f * (double) i / kSr), [] (float v) { return v; });
            std::vector<float> tail (y.begin() + 2048, y.end());
            const double db = goertzelDb (tail, f, kSr);   // window is not an integer number of cycles; approximate
            CHECK (std::fabs (db) < 0.6, "4x passband %.0f Hz: %.2f dB", f, db);
        }
    }

    // aliasing: a driven 9 kHz sine. Everything except the fundamental is alias
    // (the 3rd harmonic, 27 kHz, is above Nyquist), so the residual after
    // removing the fundamental measures aliasing directly.
    for (float gain : { 2.0f, 6.0f })
    {
        double level[3];
        int k = 0;
        for (int factor : { 1, 2, 4 })
        {
            Oversampler os; os.setFactor (factor);
            std::vector<float> y (16384);
            for (size_t i = 0; i < y.size(); ++i)
                y[i] = os.process (0.9f * (float) std::sin (2.0 * kPi * 9000.0 * (double) i / kSr),
                                   [gain] (float v) { return fastTanh (gain * v); });
            double sc = 0, ss = 0, n = 0;
            const size_t a = 4096;
            for (size_t i = a; i < y.size(); ++i)
            {
                const double w = 2.0 * kPi * 9000.0 * (double) i / kSr;
                sc += y[i] * std::cos (w); ss += y[i] * std::sin (w); n += 1.0;
            }
            sc *= 2.0 / n; ss *= 2.0 / n;
            double er = 0, ef = 0;
            for (size_t i = a; i < y.size(); ++i)
            {
                const double w = 2.0 * kPi * 9000.0 * (double) i / kSr;
                const double f = sc * std::cos (w) + ss * std::sin (w);
                er += (y[i] - f) * (y[i] - f); ef += f * f;
            }
            level[k++] = 10.0 * std::log10 (er / ef + 1.0e-30);
        }
        std::printf ("  drive x%.0f alias residual: 1x %.1f dB, 2x %.1f dB, 4x %.1f dB\n", (double) gain, level[0], level[1], level[2]);
        CHECK (level[1] < level[0] - 15.0 && level[2] <= level[1] + 0.5, "aliasing drops with oversampling (gain x%.0f)", (double) gain);
    }
}

static void testLatencyAndDry()
{
    std::printf ("\n[latency / dry path]\n");
    for (int os : { 0, 1, 2 })
    {
        Engine e; e.prepare (kSr);
        Params p; p.mix = 0.0f; p.oversampling = os;
        e.setParams (p);
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        l[0] = 1.0f; r[0] = 1.0f;
        run (e, l, r, 64);
        const int lat = e.getLatencySamples();
        CHECK (std::fabs (l[(size_t) lat] - 1.0f) < 1e-5f && std::fabs (rms (l, (size_t) lat + 1, l.size())) < 1e-6,
               "mix=0, oversampling choice %d: dry impulse appears exactly at latency %d", os, lat);
    }
}

static void testBlockSizeIndependence()
{
    std::printf ("\n[block-size independence]\n");
    Params p;
    p.distance = 0.7f; p.drive = 0.6f; p.squash = 0.5f; p.duck = 0.4f; p.voicing = 4; p.oversampling = 2; p.bias = 0.4f;
    auto in = whiteNoise (24000, 0.3f, 1);

    std::vector<float> a[2], b[2];
    for (int k = 0; k < 2; ++k)
    {
        a[k] = in; b[k] = in;
    }
    Engine e1; e1.prepare (kSr); e1.setParams (p); run (e1, a[0], a[1], 64);
    Engine e2; e2.prepare (kSr); e2.setParams (p); run (e2, b[0], b[1], 37);
    double maxDiff = 0;
    for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (a[0][i] - b[0][i]));
    CHECK (maxDiff < 1e-6, "block 64 vs 37 max difference %.2e", maxDiff);
}

static void testStability()
{
    std::printf ("\n[stability]\n");
    Params p;
    p.distance = 1.0f; p.size = 1.0f; p.decaySec = 3.0f; p.drive = 1.0f; p.bias = 1.0f; p.resonance = 1.0f;
    p.proximity = 1.0f; p.squash = 1.0f; p.duck = 1.0f; p.oversampling = 2; p.diffusion = 1.0f; p.width = 1.5f;
    p.material = 1; p.voicing = 3; p.tone = 1.0f; p.mix = 1.0f; p.outputDb = 12.0f;
    Engine e; e.prepare (kSr); e.setParams (p);
    auto l = whiteNoise ((size_t) (kSr * 8), 1.0f, 2);
    auto r = whiteNoise ((size_t) (kSr * 8), 1.0f, 3);
    run (e, l, r, 512);
    CHECK (finiteAndBounded (l, 100.0f) && finiteAndBounded (r, 100.0f),
           "extreme settings, 8 s full-scale noise: finite and bounded (peak L %.2f)", (double) *std::max_element (l.begin(), l.end()));

    // random automation of everything, every block
    Engine e2; e2.prepare (kSr);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    bool ok = true; float peak = 0;
    for (int blk = 0; blk < 800 && ok; ++blk)
    {
        Params q;
        q.distance = u (rng); q.distDepth = u (rng); q.size = u (rng); q.decaySec = 0.1f + 2.9f * u (rng);
        q.diffusion = u (rng); q.width = 1.5f * u (rng); q.roomLowCutHz = 20 + 980 * u (rng); q.roomHighCutHz = 1000 + 19000 * u (rng);
        q.material = (int) (u (rng) * 3.99f); q.voicing = (int) (u (rng) * 4.99f); q.lowCutHz = 20 + 480 * u (rng);
        q.highCutHz = 2000 + 18000 * u (rng); q.resonance = u (rng); q.proximity = u (rng); q.satType = (int) (u (rng) * 2.99f);
        q.drive = u (rng); q.bias = u (rng); q.tone = 2 * u (rng) - 1; q.satMix = u (rng); q.satPost = u (rng) > 0.5f;
        q.oversampling = (int) (u (rng) * 2.99f); q.squash = u (rng); q.duck = u (rng); q.mix = u (rng); q.outputDb = -24 + 36 * u (rng);
        e2.setParams (q);
        auto bl = whiteNoise (256, 0.5f, (unsigned) blk), br = whiteNoise (256, 0.5f, (unsigned) blk + 9999);
        e2.process (bl.data(), br.data(), 256);
        ok = finiteAndBounded (bl, 500.0f) && finiteAndBounded (br, 500.0f);
        for (float s : bl) peak = std::max (peak, std::fabs (s));
    }
    CHECK (ok, "800 blocks of fully random automation: finite and bounded (peak %.2f)", (double) peak);
}

static std::vector<float> impulseResponse (const Params& p, size_t n)
{
    Engine e; e.prepare (kSr); e.setParams (p);
    std::vector<float> l (n, 0.0f), r (n, 0.0f);
    l[0] = 1.0f; r[0] = 1.0f;
    run (e, l, r, 256);
    return l;
}

// RT60 estimate from Schroeder backward integration (T20 x 3)
static double estimateRt60 (const std::vector<float>& ir, size_t start)
{
    std::vector<double> edc (ir.size());
    double acc = 0;
    for (size_t i = ir.size(); i-- > start;) { acc += (double) ir[i] * ir[i]; edc[i] = acc; }
    const double e0 = edc[start];
    double t5 = -1, t25 = -1;
    for (size_t i = start; i < ir.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / e0 + 1e-30);
        if (t5 < 0 && db <= -5.0) t5 = (double) i;
        if (t25 < 0 && db <= -25.0) { t25 = (double) i; break; }
    }
    if (t5 < 0 || t25 < 0) return -1;
    return 3.0 * (t25 - t5) / kSr;
}

static void testRoom()
{
    std::printf ("\n[room]\n");

    // number of early reflection taps
    { EarlyReflections er; er.prepare (kSr); er.setGeometry (1.0f, 0.8f, 8000.0f);
      CHECK (er.getNumTaps() == 62, "image-source taps up to 3rd order: %d", er.getNumTaps()); }

    for (float rt : { 0.3f, 0.8f, 1.5f })
    {
        Params p; p.distance = 1.0f; p.decaySec = rt; p.size = 0.5f; p.mix = 1.0f; p.oversampling = 0;
        p.drive = 0.0f; p.satMix = 0.0f; p.material = 1;   // concrete: little HF difference
        auto ir = impulseResponse (p, (size_t) (kSr * (rt * 3.0 + 1.0)));
        const double est = estimateRt60 (ir, (size_t) (0.12 * kSr));
        std::printf ("  decay setting %.2f s -> measured RT60 %.2f s\n", (double) rt, est);
        CHECK (est > rt * 0.65 && est < rt * 1.5, "RT60 within tolerance for %.2f s", (double) rt);
    }

    // darker material -> faster HF decay (energy above 6 kHz in the tail)
    {
        double hf[4];
        for (int m = 0; m < 4; ++m)
        {
            Params p; p.distance = 1.0f; p.decaySec = 1.2f; p.material = m; p.oversampling = 0; p.satMix = 0.0f; p.roomHighCutHz = 20000.0f;
            auto ir = impulseResponse (p, (size_t) (kSr * 2.5));
            std::vector<float> tail (ir.begin() + (size_t) (0.6 * kSr), ir.end());
            // 8 kHz band level vs 500 Hz band level in the tail (average over a few bins)
            double a = 0, b = 0;
            for (double f : { 7000.0, 8000.0, 9000.0, 10000.0 }) a += std::pow (10.0, goertzelDb (tail, f, kSr) / 10.0);
            for (double f : { 400.0, 500.0, 600.0, 700.0 })       b += std::pow (10.0, goertzelDb (tail, f, kSr) / 10.0);
            hf[m] = 10.0 * std::log10 (a / b);
        }
        std::printf ("  tail HF/LF balance (dB): wood %.1f, concrete %.1f, padded %.1f, tiled %.1f\n", hf[0], hf[1], hf[2], hf[3]);
        CHECK (hf[2] < hf[0] && hf[0] < hf[1] && hf[1] < hf[3] + 1.0, "materials ordered padded < wood < concrete <~ tiled");
    }

    // level calibration of the late tail: unit-RMS noise in, tail RMS out (ER + direct muted by distance 1 mapping not applicable) -> print
    {
        LateFDN fdn; fdn.prepare (kSr); fdn.setParams (1.0f, 0.5f, 0.8f, 0.7f);
        auto n = whiteNoise ((size_t) (kSr * 6), 1.0f, 11);
        double s = 0; size_t cnt = 0;
        for (size_t i = 0; i < n.size(); ++i)
        {
            if (i % kControlBlock == 0) fdn.setParams (1.0f, 0.5f, 0.8f, 0.7f);
            float oL, oR; fdn.process (n[i], n[i], oL, oR);
            if (i > (size_t) (kSr * 2)) { s += (double) oL * oL; ++cnt; }
        }
        std::printf ("  FDN steady-state output RMS for unit-RMS input @0.5 s: %.3f (%.1f dB)\n",
                     std::sqrt (s / (double) cnt), 20.0 * std::log10 (std::sqrt (s / (double) cnt)));
    }
    {
        EarlyReflections er; er.prepare (kSr); er.setGeometry (1.0f, 0.8f, 8000.0f);
        auto n = whiteNoise ((size_t) (kSr * 3), 1.0f, 12);
        double s = 0; size_t cnt = 0;
        for (size_t i = 0; i < n.size(); ++i)
        {
            float oL, oR; er.process (n[i], oL, oR);
            if (i > (size_t) (kSr * 1)) { s += (double) oL * oL; ++cnt; }
        }
        std::printf ("  ER steady-state output RMS for unit-RMS input: %.3f (%.1f dB)\n",
                     std::sqrt (s / (double) cnt), 20.0 * std::log10 (std::sqrt (s / (double) cnt)));
    }
}

static void testDistance()
{
    std::printf ("\n[distance macro]\n");
    std::printf ("  dist  direct   ER    late  predelay  air-LP\n");
    float prev = 1.0e9f;
    bool monotonic = true;
    for (int i = 0; i <= 10; ++i)
    {
        const float d = (float) i / 10.0f;
        const DistanceMap m = mapDistance (d);
        std::printf ("  %.1f  %6.1f %6.1f %6.1f  %6.1f ms %6.0f Hz\n", (double) d, (double) m.directDb, (double) m.erDb,
                     (double) m.lateDb, (double) m.predelayMs, (double) m.airHz);
        monotonic = monotonic && m.directDb <= prev + 1e-4f;
        prev = m.directDb;
    }
    CHECK (monotonic, "direct level falls monotonically with distance");

    // end-to-end: room/direct energy ratio on noise grows with distance
    double ratio[3]; int k = 0;
    for (float d : { 0.0f, 0.5f, 1.0f })
    {
        Params p; p.distance = d; p.mix = 1.0f; p.oversampling = 0; p.satMix = 0.0f; p.voicing = 0; p.decaySec = 0.6f;
        Engine e; e.prepare (kSr); e.setParams (p);
        auto l = whiteNoise ((size_t) (kSr * 3), 0.2f, 21), r = l;
        const auto in = l;
        run (e, l, r, 256);
        ratio[k++] = rms (l, (size_t) kSr, l.size()) / rms (in, (size_t) kSr, in.size());
    }
    std::printf ("  output/input RMS on noise at distance 0 / 0.5 / 1: %.2f / %.2f / %.2f\n", ratio[0], ratio[1], ratio[2]);
    CHECK (ratio[0] > 0.3 && ratio[2] > 0.1, "levels stay in a usable range across the distance sweep");
}

static void testSaturator()
{
    std::printf ("\n[saturation]\n");
    for (int type = 0; type < 3; ++type)
    {
        Saturator s; s.prepare (kSr); s.setOversampling (2);
        s.setParams (type, 18.0f, type == 1 ? 0.6f : 0.0f, 0.0f, 1.0f);
        std::vector<float> y (32768);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = s.process (0.5f * (float) std::sin (2.0 * kPi * 1000.0 * (double) i / kSr));
        std::vector<float> t (y.begin() + 8192, y.end());
        const double h1 = goertzelDb (t, 1000.0, kSr), h2 = goertzelDb (t, 2000.0, kSr), h3 = goertzelDb (t, 3000.0, kSr);
        const char* names[3] = { "tape", "tube", "transformer" };
        std::printf ("  %-12s H1 %.1f dB, H2 %.1f dB, H3 %.1f dB\n", names[type], h1, h2, h3);
        CHECK (h3 > h2 - 60.0 && std::isfinite (h1), "%s generates harmonics", names[type]);
        if (type == 1) CHECK (h2 > h3 - 25.0 && h2 > -60.0, "tube with bias produces a strong 2nd harmonic");
    }
}

static void testCpu()
{
    std::printf ("\n[cpu]\n");
    for (int os : { 0, 1, 2 })
    {
        Params p; p.oversampling = os; p.squash = 0.5f; p.duck = 0.3f; p.voicing = 4;
        Engine e; e.prepare (kSr); e.setParams (p);
        auto l = whiteNoise ((size_t) (kSr * 10), 0.3f, 5), r = l;
        const auto t0 = std::chrono::steady_clock::now();
        run (e, l, r, 512);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  oversampling %s: 10 s of stereo audio in %.3f s -> %.1f%% of one core (this machine, -O2)\n",
                     os == 0 ? "off" : (os == 1 ? "2x" : "4x"), secs, secs / 10.0 * 100.0);
    }
}

int main()
{
#if defined(__SSE__)
    _mm_setcsr (_mm_getcsr() | 0x8040);   // flush-to-zero + denormals-are-zero, like JUCE's ScopedNoDenormals
#endif
    testOversampler();
    testLatencyAndDry();
    testBlockSizeIndependence();
    testStability();
    testRoom();
    testDistance();
    testSaturator();
    testCpu();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
