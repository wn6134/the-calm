// CPU benchmark and torture test for The Calm's processor.
//   calm_stress   -> CPU table, then randomized stress; exit code 1 if output ever goes bad.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace
{
juce::AudioBuffer<float> guitarLikeNoise (int channels, int samples, juce::Random& rng)
{
    // Pink-ish noise with a slow envelope: a rough stand-in for a played guitar.
    juce::AudioBuffer<float> b (channels, samples);
    float lp = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        lp = 0.97f * lp + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);
        const float env = 0.6f * (0.5f + 0.5f * std::sin ((float) i * 2.0f * 3.14159f / 24000.0f));
        for (int c = 0; c < channels; ++c)
            b.setSample (c, i, 6.0f * lp * env);
    }
    return b;
}

void setPlain (TheCalmProcessor& p, const char* id, float v)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

double cpuPercent (TheCalmProcessor& p, double rate, int block, int channels, double seconds)
{
    p.setPlayConfigDetails (channels, channels, rate, block);
    p.prepareToPlay (rate, block);
    juce::Random rng (1);
    const int total = (int) (rate * seconds);
    auto input = guitarLikeNoise (channels, total, rng);
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    const auto start = juce::Time::getHighResolutionTicks();
    for (int pos = 0; pos + block <= total; pos += block)
    {
        for (int c = 0; c < channels; ++c)
            buffer.copyFrom (c, 0, input, c, pos, block);
        p.processBlock (buffer, midi);
    }
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    return 100.0 * elapsed / seconds;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    int failures = 0;

    // ---------------------------------------------------------------- CPU
    std::printf ("CPU, %% of one core (10 s of audio)\n");
    std::printf ("%-28s %6s %6s %6s %6s %6s %6s\n", "setting", "32", "64", "128", "256", "512", "1024");
    struct Setting { const char* name; float panic, chaos, speed; bool oct2; bool stereo; double rate; };
    const Setting settings[] = {
        { "bypassed (switches up)", 0.5f, 0.5f, 0.5f, false, true, 48000.0 },
        { "UP, defaults, stereo", 0.5f, 0.5f, 0.5f, false, true, 48000.0 },
        { "WAY UP, all max, stereo", 1.0f, 1.0f, 1.0f, true, true, 48000.0 },
        { "WAY UP, all max, mono", 1.0f, 1.0f, 1.0f, true, false, 48000.0 },
        { "WAY UP, all max, 96 kHz", 1.0f, 1.0f, 1.0f, true, true, 96000.0 },
    };
    for (const auto& s : settings)
    {
        std::printf ("%-28s", s.name);
        for (int block : { 32, 64, 128, 256, 512, 1024 })
        {
            TheCalmProcessor p;
            setPlain (p, ParamIDs::panic, s.panic);
            setPlain (p, ParamIDs::chaos, s.chaos);
            setPlain (p, ParamIDs::speed, s.speed);
            setPlain (p, ParamIDs::stereo, s.stereo ? 1.0f : 0.0f);
            const bool engaged = std::string (s.name).find ("bypassed") == std::string::npos;
            setPlain (p, s.oct2 ? ParamIDs::octave2 : ParamIDs::octave1, engaged ? 1.0f : 0.0f);
            std::printf (" %5.2f%%", cpuPercent (p, s.rate, block, 2, 10.0));
        }
        std::printf ("\n");
    }

    // ---------------------------------------------------------------- stress
    // Two passes: everything random, then the same with Input/Output pinned at 0 dB, which shows
    // whether the feedback paths (Sloth with Order) ever run away on their own.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool unityGains = pass == 1;
        std::printf ("\nStress%s: random automation, presets, switches, block sizes and sample rates\n",
                     unityGains ? " (Input/Output at 0 dB)" : "");
        juce::Random rng (12345 + pass);
        TheCalmProcessor p;
        const auto& factory = PresetManager::factoryPresets();
        float worstPeak = 0.0f, inputPeak = 0.0f;
        long long processed = 0, badSamples = 0, denormals = 0;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        for (int round = 0; round < 40; ++round)
        {
            const double rate = rates[rng.nextInt (5)];
            const int maxBlock = 2048;
            p.setPlayConfigDetails (2, 2, rate, maxBlock);
            p.prepareToPlay (rate, maxBlock);
            auto input = guitarLikeNoise (2, (int) rate, rng); // one second per round
            inputPeak = juce::jmax (inputPeak, input.getMagnitude (0, input.getNumSamples()));
            int pos = 0;
            while (pos < input.getNumSamples())
            {
                const int block = juce::jmin (1 + rng.nextInt (maxBlock), input.getNumSamples() - pos);
                // Torture: every block, randomize a few parameters; sometimes jump presets or slam switches.
                for (int k = 0; k < 3; ++k)
                {
                    auto params = p.getParameters();
                    auto* param = params[rng.nextInt (params.size())];
                    param->setValueNotifyingHost (rng.nextFloat());
                }
                if (rng.nextInt (20) == 0)
                    p.presets.loadFactory (rng.nextInt ((int) factory.size()));
                if (rng.nextInt (8) == 0)
                    setPlain (p, rng.nextBool() ? ParamIDs::octave1 : ParamIDs::octave2, rng.nextBool() ? 1.0f : 0.0f);
                if (rng.nextInt (200) == 0)
                    p.reset();
                if (unityGains)
                {
                    setPlain (p, ParamIDs::input, 0.0f);
                    setPlain (p, ParamIDs::output, 0.0f);
                }

                juce::AudioBuffer<float> buffer (2, block);
                for (int c = 0; c < 2; ++c)
                    buffer.copyFrom (c, 0, input, c, pos, block);
                juce::MidiBuffer midi;
                p.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                    {
                        const float v = buffer.getSample (c, i);
                        if (! std::isfinite (v))
                            ++badSamples;
                        else
                        {
                            worstPeak = juce::jmax (worstPeak, std::abs (v));
                            if (std::fpclassify (v) == FP_SUBNORMAL)
                                ++denormals;
                        }
                    }
                processed += block;
                pos += block;
            }
        }
        std::printf ("processed %lld samples per channel; input peak %.1f dBFS, output peak %.1f dBFS; NaN/Inf %lld, denormals %lld\n",
                     processed, juce::Decibels::gainToDecibels (inputPeak), juce::Decibels::gainToDecibels (worstPeak), badSamples, denormals);
        // With unity gains the output should stay within a few dB of the input (the comb resonates).
        const float ceiling = unityGains ? inputPeak * 4.0f : inputPeak * 1000.0f;
        const bool ok = badSamples == 0 && denormals == 0 && worstPeak < ceiling;
        failures += ok ? 0 : 1;
        std::printf ("%s\n", ok ? "STRESS PASSED" : "STRESS FAILED");
    }
    return failures == 0 ? 0 : 1;
}
