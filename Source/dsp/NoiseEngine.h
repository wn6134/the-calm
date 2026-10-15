// The Calm DSP core. Header-only, no JUCE dependency, so the offline renderer can use it too.
//
// Signal flow (per channel):
//   in -> input gain -> gate -> feedback comb (Speed) -> 3 grain shifters (main + Panic voices) -> wet
//   out = mix(dry, wet, blend * engaged) * output gain
//
// Every constant below was measured on the reference plugin (see tools/analyze.py):
//   PANIC  adds two detuned voices: -250 and +350 cents at full, level panic^1.2
//   CHAOS  random pitch drift + random grain start offsets
//   SPEED  negative feedback comb (6 ms) ahead of the shifter, g = 0.94 * speed^2.4;
//          Chaos wobbles its delay by up to +-45%
//   RISE   linear glide to the full shift over the Rise time; release is instant
//   SHIFT  two taps, equal-power crossfade, 2300-sample window at 48 kHz,
//          anti-alias lowpass at fs / (2 * ratio) on the input (H(z^2) form above +1 octave,
//          mirror band and all, like the reference)
//   OCT+1 / OCT+2 footswitches
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace calm
{
constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// Fast deterministic noise in [-1, 1].
struct Rng
{
    uint32_t state = 0x9E3779B9u;
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

struct OnePole
{
    float z = 0.0f, coeff = 0.0f;
    void setTime (double sampleRate, float seconds)
    {
        coeff = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (float) (seconds * sampleRate));
    }
    float process (float x) { return z = x + coeff * (z - x); }
};

// Delay-line pitch shifter: two read taps sweep across a window with equal-power crossfades.
// Crude on purpose: the grain artifacts are part of the sound.
class PitchShifter
{
public:
    void prepare (double sampleRate, float windowMs)
    {
        size_t size = 1;
        while (size < (size_t) (sampleRate * 0.25))
            size <<= 1;
        buffer.assign (size, 0.0f);
        mask = size - 1;
        window = (float) (windowMs * 0.001 * sampleRate);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        tapPhase[0] = 0.0f;
        tapPhase[1] = 0.5f;
        tapRatio[0] = tapRatio[1] = 1.0f;
        tapOffset[0] = tapOffset[1] = 0.0f;
    }

    // grainJitterSemis: each new grain gets a random pitch offset within +-this range.
    // grainOffsetSamples: each new grain starts reading up to this much further back.
    float process (float x, float ratio, float grainJitterSemis, float grainOffsetSamples, Rng& rng)
    {
        buffer[writePos] = x;

        // Advance both taps first, then start new grains relative to the other tap's *current*
        // phase, so the pair stays exactly half a window apart (any lag adds odd grain lines).
        bool wrapped[2];
        for (int tap = 0; tap < 2; ++tap)
        {
            float& p = tapPhase[tap];
            p += (1.0f - ratio * tapRatio[tap]) / window;
            wrapped[tap] = p < 0.0f || p >= 1.0f;
        }
        for (int tap = 0; tap < 2; ++tap)
        {
            if (! wrapped[tap])
                continue;
            // New grain: start half a window away from the other tap, with a fresh jitter.
            float other = tapPhase[1 - tap] + 0.5f;
            if (wrapped[1 - tap])
                other = tapPhase[1 - tap] - std::floor (tapPhase[1 - tap]) + 0.5f;
            tapPhase[tap] = other - std::floor (other);
            tapRatio[tap] = grainJitterSemis > 0.0f ? std::exp2 (grainJitterSemis * rng.next() / 12.0f) : 1.0f;
            tapOffset[tap] = grainOffsetSamples * (0.5f + 0.5f * rng.next());
        }

        float out = 0.0f;
        for (int tap = 0; tap < 2; ++tap)
        {
            const float p = tapPhase[tap];
            // Equal-power (sin/cos) crossfade, measured on the reference.
            out += std::sin (kPi * p) * read (p * window + 2.0f + tapOffset[tap]);
        }

        writePos = (writePos + 1) & mask;
        return out;
    }

private:
    float read (float delay) const
    {
        const float pos = (float) writePos - delay;
        const float fl = std::floor (pos);
        const float t = pos - fl;
        const auto i = (size_t) (int64_t) fl;
        if (interpolation == 0)
            return buffer[(t < 0.5f ? i : i + 1) & mask];
        if (interpolation == 1)
            return buffer[i & mask] + t * (buffer[(i + 1) & mask] - buffer[i & mask]);
        const float xm1 = buffer[(i - 1) & mask], x0 = buffer[i & mask];
        const float x1 = buffer[(i + 1) & mask], x2 = buffer[(i + 2) & mask];
        // 4-point Hermite
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

public:
    int interpolation = 2; // 0 nearest, 1 linear, 2 Hermite

private:
    std::vector<float> buffer;
    size_t mask = 0, writePos = 0;
    float window = 1.0f;
    float tapPhase[2] { 0.0f, 0.5f };
    float tapRatio[2] { 1.0f, 1.0f };
    float tapOffset[2] { 0.0f, 0.0f };
};

// Anti-alias filter in front of the shifter, modelled on the reference:
//  - ratio <= 2: 8th-order elliptic lowpass at fs / (2 * ratio);
//  - ratio  > 2: the same elliptic in H(z^2) form (run separately on even and odd samples),
//    designed at fs / 2. That leaves a mirror passband near fs / 2, which the reference has too
//    (its +2 octave aliases high input frequencies all the way down), followed by a 2nd-order
//    lowpass that takes the mirror down by ~18 dB.
class AntiAliasFilter
{
public:
    void setCutoff (double sampleRate, double ratio)
    {
        const bool mirrored = ratio > 2.0;
        if (mirrored != polyphase)
            reset();
        polyphase = mirrored;

        const double rate = polyphase ? 0.5 * sampleRate : sampleRate;
        const double cutoff = std::min (0.5 * sampleRate / std::max (1.0, ratio), 0.49 * rate);
        design (rate, cutoff);

        // Mirror-taming lowpass, only in polyphase mode (Butterworth, Q = 0.707).
        if (polyphase)
        {
            const double w = std::tan (3.14159265358979 * std::min (3.5 * cutoff, 0.45 * sampleRate) / sampleRate);
            const double q = 0.7071, n = 1.0 / (1.0 + w / q + w * w);
            tame.b0 = (float) (w * w * n);
            tame.b1 = 2.0f * tame.b0;
            tame.b2 = tame.b0;
            tame.a1 = (float) (2.0 * (w * w - 1.0) * n);
            tame.a2 = (float) ((1.0 - w / q + w * w) * n);
        }
    }

    void reset()
    {
        for (auto& phase : state)
            for (auto& st : phase)
                st = {};
        tame.z1 = tame.z2 = 0.0f;
        odd = false;
    }

    float process (float x)
    {
        auto& st = state[polyphase && odd ? 1 : 0];
        odd = ! odd;
        for (int i = 0; i < 4; ++i)
            x = coeffs[i].run (x, st[i]);
        if (polyphase)
        {
            Biquad::State& ts = tameState;
            x = tame.run (x, ts);
        }
        return x;
    }

private:
    struct Biquad
    {
        struct State { float z1 = 0, z2 = 0; };
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run (float x, State& s) const
        {
            const float y = b0 * x + s.z1;
            s.z1 = b1 * x - a1 * y + s.z2;
            s.z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void design (double rate, double cutoffHz)
    {
        // Analog prototype from scipy.signal.ellipap(8, 0.5, 60), normalised to 1 rad/s.
        static constexpr double zeroIm[4] = { 4.11309337, 1.61111820, 1.23066458, 1.13370950 };
        static constexpr double poleRe[4] = { -0.30196340, -0.17571042, -0.07215169, -0.01825257 };
        static constexpr double poleIm[4] = { 0.29742061, 0.73406604, 0.93480429, 1.00304179 };

        const double wc = 2.0 * rate * std::tan (3.14159265358979 * cutoffHz / rate);
        const double k = 2.0 * rate, k2 = k * k;
        for (int i = 0; i < 4; ++i)
        {
            // Section: (s^2 + wz^2) / (s^2 - 2 Re(p) s + |p|^2), frequency-scaled by wc, unity DC gain.
            const double wz2 = zeroIm[i] * zeroIm[i] * wc * wc;
            const double a1 = -2.0 * poleRe[i] * wc;
            const double a0 = (poleRe[i] * poleRe[i] + poleIm[i] * poleIm[i]) * wc * wc;
            const double g = a0 / wz2;
            const double d = k2 + a1 * k + a0;
            auto& b = coeffs[i];
            b.b0 = (float) (g * (k2 + wz2) / d);
            b.b1 = (float) (g * 2.0 * (wz2 - k2) / d);
            b.b2 = b.b0;
            b.a1 = (float) (2.0 * (a0 - k2) / d);
            b.a2 = (float) ((k2 - a1 * k + a0) / d);
        }
    }

    Biquad coeffs[4];
    Biquad::State state[2][4];
    Biquad tame;
    Biquad::State tameState;
    bool polyphase = false, odd = false;
};

// Output rolloff. The reference rolls off like an analog 2nd-order lowpass (18.1 kHz, Q 0.65) at every
// sample rate, so this uses Vicanek's "matched" design, which keeps that analog response right up to
// Nyquist instead of the bilinear transform's forced zero there.
struct OutputTone
{
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    void set (double sampleRate, double cutoffHz, double q)
    {
        const double w0 = 2.0 * 3.14159265358979 * cutoffHz / sampleRate;
        const double zeta = 1.0 / (2.0 * q);
        const double decay = std::exp (-zeta * w0);
        const double ca1 = zeta <= 1.0 ? -2.0 * decay * std::cos (std::sqrt (1.0 - zeta * zeta) * w0)
                                       : -2.0 * decay * std::cosh (std::sqrt (zeta * zeta - 1.0) * w0);
        const double ca2 = decay * decay;
        const double A0 = (1.0 + ca1 + ca2) * (1.0 + ca1 + ca2);
        const double A1 = (1.0 - ca1 + ca2) * (1.0 - ca1 + ca2);
        const double A2 = -4.0 * ca2;
        const double phi1 = std::sin (0.5 * w0) * std::sin (0.5 * w0);
        const double phi0 = 1.0 - phi1;
        const double phi2 = 4.0 * phi0 * phi1;
        const double R1 = (A0 * phi0 + A1 * phi1 + A2 * phi2) * q * q;
        const double B1 = std::max (0.0, (R1 - A0 * phi0) / phi1);
        const double cb0 = 0.5 * (std::sqrt (A0) + std::sqrt (B1));
        b0 = (float) cb0;
        b1 = (float) (std::sqrt (A0) - cb0);
        a1 = (float) ca1;
        a2 = (float) ca2;
        z1 = z2 = 0.0f;
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = -a2 * y;
        return y;
    }
};

// Feedback comb with negative feedback on the shifter input: y = x - g * y[n - D].
// Its resonances land on odd multiples of 1 / (2D) and are carried up by the shift.
// Chaos wobbles D, which smears the resonances the way the reference does.
class FeedbackComb
{
public:
    void prepare (int maxDelaySamples)
    {
        size_t size = 1;
        while (size < (size_t) maxDelaySamples + 4)
            size <<= 1;
        line.assign (size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        pos = 0;
    }

    float process (float x, float g, float delaySamples)
    {
        const float rd = (float) pos - delaySamples;
        const float fl = std::floor (rd);
        const float t = rd - fl;
        const auto i = (size_t) (int64_t) fl;
        const float fb = line[i & mask] + t * (line[(i + 1) & mask] - line[i & mask]);
        const float y = x - g * fb;
        line[pos] = y;
        pos = (pos + 1) & mask;
        return y;
    }

private:
    std::vector<float> line;
    size_t mask = 0, pos = 0;
};

struct Params
{
    float inputDb = 0.0f;
    float gateDb = -80.0f;     // -80 = off
    float blend = 1.0f;        // 0..1, wet share
    float outputDb = 0.0f;
    float panic = 0.0f;        // 0..1
    float chaos = 0.0f;        // 0..1
    float speed = 0.0f;        // 0..1
    float riseMs = 0.7f;
    // Chaos model constants, fitted to the reference; exposed for the offline renderer.
    float chaosGlideSemis = 2.2f;  // smooth random pitch drift at full chaos (scaled by chaos^1.5)
    float chaosGrainSemis = 0.0f;  // per-grain random pitch at full chaos
    float chaosOffsetMs = 20.0f;   // per-grain random start offset at full chaos
    // Speed model constants, fitted to the reference.
    float speedMaxFeedback = 0.94f;
    float speedCurve = 2.4f;
    float speedTrim = 0.1f;
    float speedChaosMod = 0.45f;  // relative comb-delay wobble at full chaos (fitted on DI guitar)
    float speedChaosSmoothMs = 3.0f; // extra smoothing of that wobble (sets how much low end it generates)
    bool octave1 = false;
    bool octave2 = false;
    bool stereo = false;
};

class NoiseEngine
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& ch : channels)
        {
            for (auto& v : ch.voices)
                v.prepare (sampleRate, windowMs);
            ch.comb.prepare ((int) std::lround (4.0 * combMs * 0.001 * sampleRate));
            ch.antiAlias.setCutoff (sampleRate, 1.0);
            ch.tone.set (sampleRate, 18062.0, 0.649);
        }
        for (auto* s : { &panicSmooth, &chaosSmooth, &speedSmooth, &blendSmooth, &inSmooth, &outSmooth })
            s->setTime (sampleRate, 0.02f);
        chaosFilter.setTime (sampleRate, 0.01f);
        engageSmooth.setTime (sampleRate, 0.004f);
        gateEnvAttack = std::exp (-1.0f / (float) (0.001 * sampleRate));
        gateEnvRelease = std::exp (-1.0f / (float) (0.05 * sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& ch : channels)
        {
            for (auto& v : ch.voices)
                v.reset();
            ch.comb.reset();
            ch.antiAlias.reset();
            ch.tone.z1 = ch.tone.z2 = 0.0f;
            ch.gateEnv = 0.0f;
            ch.gateGain = 0.0f;
        }
        semitones = 0.0f;
        chaosFilter.z = 0.0f;
        combWobble = 0.0f;
        engageSmooth.z = 0.0f;
        primed = false;
        lastOctaves = 0;
        riseStep = 0.0f;
        chaosTarget = chaosValue = 0.0f;
        chaosCounter = 0;
        rng.state = seed;
    }

    // Chaos is random but repeatable: every reset() restarts the sequence from this seed.
    void setInterpolation (int mode)
    {
        for (auto& ch : channels)
            for (auto& v : ch.voices)
                v.interpolation = mode;
    }

    void setSeed (uint32_t newSeed)
    {
        seed = newSeed ? newSeed : 0x9E3779B9u;
        rng.state = seed;
    }

    // Process in place. numChannels is 1 or 2.
    void process (float* const* data, int numChannels, int numSamples, const Params& p)
    {
        const int octaves = p.octave2 ? 2 : (p.octave1 ? 1 : 0);
        const float target = 12.0f * (float) octaves;
        // Rise is the time to reach the new target from wherever we are; release is instant.
        if (octaves != lastOctaves)
        {
            const float riseSamples = p.riseMs * 0.001f * (float) sampleRate;
            if (target < semitones || riseSamples < 1.0f)
                semitones = target;
            riseStep = (target - semitones) / std::max (1.0f, riseSamples);
            lastOctaves = octaves;
        }
        const bool monoSum = numChannels == 2 && ! p.stereo;
        const int procChannels = monoSum ? 1 : numChannels;
        const float gateThresh = p.gateDb <= -79.9f ? 0.0f : dbToGain (p.gateDb);

        combWobbleCoeff = p.speedChaosSmoothMs > 0.0f ? std::exp (-1000.0f / (p.speedChaosSmoothMs * (float) sampleRate)) : 0.0f;
        if (! primed)
        {
            // Start smoothers at the current settings instead of ramping from stale values.
            panicSmooth.z = p.panic;
            chaosSmooth.z = p.chaos;
            speedSmooth.z = p.speed;
            blendSmooth.z = p.blend;
            inSmooth.z = dbToGain (p.inputDb);
            outSmooth.z = dbToGain (p.outputDb);
            primed = true;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            // Rise: linear glide in semitones towards the footswitch target.
            if (semitones < target)
                semitones = std::min (target, semitones + riseStep);

            const float engaged = engageSmooth.process (target > 0.0f || semitones > 0.01f ? 1.0f : 0.0f);
            const float panic = panicSmooth.process (p.panic);
            const float chaos = chaosSmooth.process (p.chaos);
            const float speed = speedSmooth.process (p.speed);
            const float blend = blendSmooth.process (p.blend);
            const float inGain = inSmooth.process (dbToGain (p.inputDb));
            const float outGain = outSmooth.process (dbToGain (p.outputDb));

            // Chaos drift: smoothed sample-and-hold, new random target every 15-45 ms.
            if (--chaosCounter <= 0)
            {
                chaosTarget = rng.next();
                chaosCounter = (int) (sampleRate * (0.03 + 0.015 * rng.next()));
            }
            chaosValue = chaosFilter.process (chaosTarget);
            // Chaos = smooth random pitch drift + random start offset for every new grain.
            // The offsets scramble grain phases, which gives the reference its amplitude dips.
            const float chaosSemis = chaosValue * p.chaosGlideSemis * chaos * std::sqrt (chaos);
            const float grainJitter = chaos * p.chaosGrainSemis;


            // Panic: main voice plus one voice below and one above, all following the same glide.
            const float base = semitones + chaosSemis;
            const float ratioMain = std::exp2 (base / 12.0f);
            if ((i & 31) == 0)
                for (int c = 0; c < procChannels; ++c)
                    channels[c].antiAlias.setCutoff (sampleRate, std::exp2 (semitones / 12.0f)); // glide only, not Chaos
            const float ratioDown = std::exp2 ((base - 2.5f * panic) / 12.0f);
            const float ratioUp = std::exp2 ((base + 3.5f * panic) / 12.0f);
            const float grainOffset = chaos * p.chaosOffsetMs * 0.001f * (float) sampleRate;
            const float panicLevel = std::pow (panic, 1.2f);
            const float panicNorm = 1.0f / std::sqrt (1.0f + panicLevel * panicLevel);

            // Speed: comb feedback g = 0.94 * speed^2.4 (fitted on noise and plucks), about -1 dB of make-down.
            const float combGain = p.speedMaxFeedback * std::pow (speed, p.speedCurve);
            const float combTrim = 1.0f - p.speedTrim * speed;
            combWobble = chaosValue + combWobbleCoeff * (combWobble - chaosValue);
            const float combDelay = (float) (combMs * 0.001 * sampleRate) * (1.0f + p.speedChaosMod * chaos * combWobble);

            if (monoSum)
                data[0][i] = 0.5f * (data[0][i] + data[1][i]);

            for (int c = 0; c < procChannels; ++c)
            {
                auto& ch = channels[(size_t) c];
                float x = data[c][i] * inGain;

                if (gateThresh > 0.0f)
                {
                    const float a = std::abs (x);
                    ch.gateEnv = a + (a > ch.gateEnv ? gateEnvAttack : gateEnvRelease) * (ch.gateEnv - a);
                    const float g = ch.gateEnv > gateThresh ? 1.0f : 0.0f;
                    ch.gateGain = g + (g > ch.gateGain ? gateEnvAttack : gateEnvRelease) * (ch.gateGain - g);
                    x *= ch.gateGain;
                }

                const float shifterIn = ch.comb.process (ch.antiAlias.process (x), combGain, combDelay) * combTrim;
                float wet = ch.voices[0].process (shifterIn, ratioMain, grainJitter, grainOffset, rng);
                const float down = ch.voices[1].process (shifterIn, ratioDown, grainJitter, grainOffset, rng);
                const float up = ch.voices[2].process (shifterIn, ratioUp, grainJitter, grainOffset, rng);
                wet = (wet + panicLevel * (down + up)) * panicNorm;

                // The reference's dry path runs 0.57 dB hot; both paths share the output rolloff.
                const float dry = x * kDryGain;
                const float mix = blend * engaged;
                data[c][i] = ch.tone.process (dry + mix * (wet - dry)) * outGain;
            }

            if (monoSum)
                data[1][i] = data[0][i];
        }
    }

private:
    struct Channel
    {
        PitchShifter voices[3];
        FeedbackComb comb;
        AntiAliasFilter antiAlias;
        OutputTone tone;
        float gateEnv = 0.0f, gateGain = 0.0f;
    };

    static constexpr float kDryGain = 1.068f;
    double sampleRate = 48000.0;
    float windowMs = 2300.0f / 48.0f; // 2300 samples at 48 kHz, measured
    float combMs = 289.0f / 48.0f; // 289 samples at 48 kHz, measured
    Channel channels[maxChannels];
    OnePole panicSmooth, chaosSmooth, speedSmooth, blendSmooth, inSmooth, outSmooth;
    OnePole chaosFilter, engageSmooth;
    float gateEnvAttack = 0.0f, gateEnvRelease = 0.0f;
    float semitones = 0.0f, riseStep = 0.0f;
    int lastOctaves = 0;
    float chaosTarget = 0.0f, chaosValue = 0.0f;
    int chaosCounter = 0;
    Rng rng;
    uint32_t seed = 0x9E3779B9u;
    bool primed = false;
    float combWobble = 0.0f, combWobbleCoeff = 0.0f;
};
} // namespace calm
