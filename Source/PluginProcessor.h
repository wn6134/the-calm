#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/NoiseEngine.h"
#include "PresetManager.h"
#include "MidiLearn.h"

namespace ParamIDs
{
inline constexpr auto input = "input";
inline constexpr auto gate = "gate";
inline constexpr auto blend = "blend";
inline constexpr auto output = "output";
inline constexpr auto panic = "panic";
inline constexpr auto chaos = "chaos";
inline constexpr auto speed = "speed";
inline constexpr auto rise = "rise";
inline constexpr auto riseSync = "riseSync";
inline constexpr auto riseDiv = "riseDiv";
inline constexpr auto octave1 = "octave1";
inline constexpr auto octave2 = "octave2";
inline constexpr auto latch1 = "latch1";
inline constexpr auto latch2 = "latch2";
inline constexpr auto stereo = "stereo";
} // namespace ParamIDs

class TheCalmProcessor final : public juce::AudioProcessor
{
public:
    TheCalmProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    // Factory presets double as host programs.
    int getNumPrograms() override { return (int) PresetManager::factoryPresets().size(); }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override { presets.loadFactory (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& riseDivisionNames();

    juce::AudioProcessorValueTreeState state;
    PresetManager presets { state };
    MidiLearn midiLearn { state };

    // Editor width remembered with the project (0 = default size).
    int getSavedEditorWidth() const { return state.state.getProperty ("uiWidth", 0); }
    void setSavedEditorWidth (int width) { state.state.setProperty ("uiWidth", width, nullptr); }

    // True while the effect is audibly engaged; read by the editor for the LED.
    std::atomic<bool> engaged { false };

    // Peak levels (linear) since the editor last read them: input L/R, output L/R.
    std::atomic<float> peaks[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float takePeak (int index) { return peaks[index].exchange (0.0f, std::memory_order_relaxed); }

private:
    float riseMilliseconds() const;

    calm::NoiseEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheCalmProcessor)
};
