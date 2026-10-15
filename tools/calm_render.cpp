// Offline renderer for The Calm DSP core.
//
//   calm_render in.wav out.wav [key=value ...] [oct1=ON_S:OFF_S] [oct2=ON_S:OFF_S]
//
// Keys: input gate blend output panic chaos speed rise stereo (0/1).
// oct1/oct2 take the press and release time in seconds; omit OFF_S to hold to the end.
// Output is 32-bit float WAV with the input's channel count and sample rate.
#include "dsp/NoiseEngine.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Audio
{
    int channels = 0;
    int sampleRate = 0;
    std::vector<std::vector<float>> data;
};

template <typename T>
T readLE (const unsigned char* p)
{
    T v;
    std::memcpy (&v, p, sizeof (T));
    return v;
}

bool readWav (const char* path, Audio& a)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (f)), {});
    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) || std::memcmp (bytes.data() + 8, "WAVE", 4))
        return false;

    int format = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        const auto* chunk = bytes.data() + pos;
        const auto size = readLE<uint32_t> (chunk + 4);
        if (! std::memcmp (chunk, "fmt ", 4))
        {
            format = readLE<uint16_t> (chunk + 8);
            a.channels = readLE<uint16_t> (chunk + 10);
            a.sampleRate = (int) readLE<uint32_t> (chunk + 12);
            bits = readLE<uint16_t> (chunk + 22);
            if (format == 0xFFFE)
                format = readLE<uint16_t> (chunk + 32);
        }
        else if (! std::memcmp (chunk, "data", 4))
        {
            const int bytesPer = bits / 8;
            const size_t frames = size / (size_t) (bytesPer * a.channels);
            a.data.assign ((size_t) a.channels, std::vector<float> (frames));
            const auto* d = chunk + 8;
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < a.channels; ++c, d += bytesPer)
                {
                    float v = 0.0f;
                    if (format == 3 && bits == 32)
                        v = readLE<float> (d);
                    else if (bits == 16)
                        v = (float) readLE<int16_t> (d) / 32768.0f;
                    else if (bits == 24)
                        v = (float) ((int32_t) ((uint32_t) d[0] << 8 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 24) >> 8) / 8388608.0f;
                    else if (bits == 32)
                        v = (float) readLE<int32_t> (d) / 2147483648.0f;
                    a.data[(size_t) c][i] = v;
                }
            return format == 1 || format == 3;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

bool writeWav (const char* path, const Audio& a)
{
    const auto frames = (uint32_t) a.data[0].size();
    const uint32_t dataSize = frames * (uint32_t) a.channels * 4;
    std::FILE* f = std::fopen (path, "wb");
    if (! f)
        return false;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    u32 (36 + dataSize);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16);
    u16 (3);
    u16 ((uint16_t) a.channels);
    u32 ((uint32_t) a.sampleRate);
    u32 ((uint32_t) a.sampleRate * (uint32_t) a.channels * 4);
    u16 ((uint16_t) (a.channels * 4));
    u16 (32);
    std::fwrite ("data", 1, 4, f);
    u32 (dataSize);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < a.channels; ++c)
            std::fwrite (&a.data[(size_t) c][i], 4, 1, f);
    std::fclose (f);
    return true;
}

struct Window
{
    double on = -1.0, off = 1e30;
    bool active (double t) const { return on >= 0.0 && t >= on && t < off; }
};

Window parseWindow (const std::string& s)
{
    Window w;
    const auto colon = s.find (':');
    w.on = std::stod (s.substr (0, colon));
    if (colon != std::string::npos)
        w.off = std::stod (s.substr (colon + 1));
    return w;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: calm_render in.wav out.wav [key=value ...]\n");
        return 1;
    }

    Audio audio;
    if (! readWav (argv[1], audio) || audio.channels < 1 || audio.channels > 2)
    {
        std::fprintf (stderr, "cannot read %s (need mono/stereo PCM or float WAV)\n", argv[1]);
        return 1;
    }

    calm::Params p;
    Window oct1, oct2;
    uint32_t seed = 0;
    int interp = -1;
    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto eq = arg.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = arg.substr (0, eq);
        const auto value = arg.substr (eq + 1);
        if (key == "oct1") oct1 = parseWindow (value);
        else if (key == "oct2") oct2 = parseWindow (value);
        else
        {
            const std::map<std::string, float*> floats {
                { "input", &p.inputDb }, { "gate", &p.gateDb }, { "blend", &p.blend }, { "output", &p.outputDb },
                { "panic", &p.panic }, { "chaos", &p.chaos }, { "speed", &p.speed }, { "rise", &p.riseMs },
                { "chaosglide", &p.chaosGlideSemis }, { "chaosgrain", &p.chaosGrainSemis }, { "chaosoffset", &p.chaosOffsetMs },
                { "speedmax", &p.speedMaxFeedback }, { "speedcurve", &p.speedCurve }, { "speedtrim", &p.speedTrim }, { "speedchaos", &p.speedChaosMod }, { "speedchaossmooth", &p.speedChaosSmoothMs } };
            if (key == "stereo") p.stereo = std::stof (value) > 0.5f;
            else if (key == "seed") seed = (uint32_t) std::stoul (value);
            else if (key == "interp") interp = std::stoi (value);
            else if (auto it = floats.find (key); it != floats.end()) *it->second = std::stof (value);
            else std::fprintf (stderr, "unknown key %s\n", key.c_str());
        }
    }

    calm::NoiseEngine engine;
    engine.prepare (audio.sampleRate);
    if (seed != 0)
        engine.setSeed (seed);
    if (interp >= 0)
        engine.setInterpolation (interp);

    constexpr int block = 64;
    const int frames = (int) audio.data[0].size();
    for (int start = 0; start < frames; start += block)
    {
        const int n = std::min (block, frames - start);
        const double t = (double) start / audio.sampleRate;
        p.octave1 = oct1.active (t);
        p.octave2 = oct2.active (t);
        float* ptrs[2] = { audio.data[0].data() + start, audio.channels > 1 ? audio.data[1].data() + start : nullptr };
        engine.process (ptrs, audio.channels, n, p);
    }

    if (! writeWav (argv[2], audio))
    {
        std::fprintf (stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
