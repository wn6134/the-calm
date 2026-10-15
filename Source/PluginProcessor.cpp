#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Rise divisions for tempo sync, expressed in quarter notes.
constexpr float riseDivisionBeats[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

float rawValue (const juce::AudioProcessorValueTreeState& s, const char* id)
{
    return s.getRawParameterValue (id)->load();
}
} // namespace

const juce::StringArray& TheCalmProcessor::riseDivisionNames()
{
    static const juce::StringArray names { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout TheCalmProcessor::createLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    using B = AudioParameterBool;

    auto dbAttr = AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    // At its minimum the gate is bypassed altogether, so it reads "Off" there.
    auto gateAttr = AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return v <= -79.9f ? String ("Off") : String (v, 1) + " dB"; })
                        .withValueFromStringFunction ([] (const String& t) { return t.trim().equalsIgnoreCase ("off") ? -80.0f : t.getFloatValue(); });
    auto pctAttr = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto msAttr = AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int) { return v < 1000.0f ? String (v, 2) + " ms" : String (v / 1000.0f, 2) + " s"; });

    NormalisableRange<float> riseRange (0.0f, 1500.0f);
    riseRange.setSkewForCentre (150.0f);

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::input, 1 }, "Input", NormalisableRange<float> (-24.0f, 24.0f), 0.0f, dbAttr),
                std::make_unique<P> (ParameterID { ParamIDs::gate, 1 }, "Gate", NormalisableRange<float> (-80.0f, 0.0f), -80.0f, gateAttr),
                std::make_unique<P> (ParameterID { ParamIDs::blend, 1 }, "Blend", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr),
                std::make_unique<P> (ParameterID { ParamIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f), 0.0f, dbAttr),
                std::make_unique<P> (ParameterID { ParamIDs::panic, 1 }, "Chill", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr),
                std::make_unique<P> (ParameterID { ParamIDs::chaos, 1 }, "Order", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr),
                std::make_unique<P> (ParameterID { ParamIDs::speed, 1 }, "Sloth", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr),
                std::make_unique<P> (ParameterID { ParamIDs::rise, 1 }, "Rise", riseRange, 0.7f, msAttr),
                std::make_unique<B> (ParameterID { ParamIDs::riseSync, 1 }, "Rise Tempo Sync", false),
                std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::riseDiv, 1 }, "Rise Division", riseDivisionNames(), 2),
                std::make_unique<B> (ParameterID { ParamIDs::octave1, 1 }, "Up", false),
                std::make_unique<B> (ParameterID { ParamIDs::octave2, 1 }, "Way Up", false),
                std::make_unique<B> (ParameterID { ParamIDs::latch1, 1 }, "Up Latch", false),
                std::make_unique<B> (ParameterID { ParamIDs::latch2, 1 }, "Way Up Latch", false),
                std::make_unique<B> (ParameterID { ParamIDs::stereo, 1 }, "Stereo", true));
    return layout;
}

TheCalmProcessor::TheCalmProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "TheCalm", createLayout())
{
}

bool TheCalmProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto& in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void TheCalmProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    engine.prepare (sampleRate);
}

float TheCalmProcessor::riseMilliseconds() const
{
    if (rawValue (state, ParamIDs::riseSync) < 0.5f)
        return rawValue (state, ParamIDs::rise);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                bpm = *hostBpm;

    const auto index = juce::jlimit (0, (int) std::size (riseDivisionBeats) - 1, (int) rawValue (state, ParamIDs::riseDiv));
    return (float) (riseDivisionBeats[index] * 60000.0 / bpm);
}

void TheCalmProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.process (midi);

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Mono in, stereo out: duplicate the input before processing.
    if (numIn == 1 && numOut == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    calm::Params p;
    p.inputDb = rawValue (state, ParamIDs::input);
    p.gateDb = rawValue (state, ParamIDs::gate);
    p.blend = rawValue (state, ParamIDs::blend);
    p.outputDb = rawValue (state, ParamIDs::output);
    p.panic = rawValue (state, ParamIDs::panic);
    p.chaos = rawValue (state, ParamIDs::chaos);
    p.speed = rawValue (state, ParamIDs::speed);
    p.riseMs = riseMilliseconds();
    p.octave1 = rawValue (state, ParamIDs::octave1) > 0.5f;
    p.octave2 = rawValue (state, ParamIDs::octave2) > 0.5f;
    p.stereo = rawValue (state, ParamIDs::stereo) > 0.5f;

    const int channels = juce::jmin (numOut, 2);
    auto storePeaks = [&] (int first, float gain) {
        for (int c = 0; c < 2; ++c)
        {
            const float level = gain * buffer.getMagnitude (juce::jmin (c, channels - 1), 0, numSamples);
            auto& slot = peaks[first + c];
            float prev = slot.load (std::memory_order_relaxed);
            while (level > prev && ! slot.compare_exchange_weak (prev, level, std::memory_order_relaxed)) {}
        }
    };

    // The input meter shows the signal after the Input knob.
    storePeaks (0, juce::Decibels::decibelsToGain (p.inputDb));
    engine.process (buffer.getArrayOfWritePointers(), channels, numSamples, p);
    storePeaks (2, 1.0f);
    engaged.store (p.octave1 || p.octave2, std::memory_order_relaxed);
}

int TheCalmProcessor::getCurrentProgram()
{
    const auto& factory = PresetManager::factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
        if (presets.currentName() == factory[i].name)
            return (int) i;
    return 0;
}

const juce::String TheCalmProcessor::getProgramName (int index)
{
    const auto& factory = PresetManager::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) factory.size()) ? juce::String (factory[(size_t) index].name) : juce::String();
}

juce::AudioProcessorEditor* TheCalmProcessor::createEditor()
{
    return new TheCalmEditor (*this);
}

void TheCalmProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    midiLearn.writeTo (state.state);
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TheCalmProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            midiLearn.readFrom (state.state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TheCalmProcessor();
}
