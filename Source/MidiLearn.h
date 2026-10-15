#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Maps MIDI controllers and notes (any channel) to parameters. The editor arms a parameter for
// learning; the next CC or note-on that arrives on the audio thread gets bound to it.
//
// CCs: value / 127, footswitches treat >= 64 as pressed.
// Notes: note-on presses, note-off releases (footswitches); bool parameters toggle on note-on;
//        knobs take the note-on velocity.
class MidiLearn
{
public:
    explicit MidiLearn (juce::AudioProcessorValueTreeState&);

    void arm (const juce::String& paramId);
    void disarm() { armed.store (-1); }
    juce::String armedParameter() const;

    int ccFor (const juce::String& paramId) const;   // -1 when no CC is bound
    int noteFor (const juce::String& paramId) const; // -1 when no note is bound
    juce::String describe (const juce::String& paramId) const; // "CC 64", "Note C3" or empty
    void clear (const juce::String& paramId);

    // Audio thread: learns and applies messages.
    void process (const juce::MidiBuffer&);

    // Persistence inside the plugin state.
    void writeTo (juce::ValueTree& state) const;
    void readFrom (const juce::ValueTree& state);

private:
    // Slots 0..127 are CC numbers, 128..255 are note numbers.
    static constexpr int noteOffset = 128;
    int indexOf (const juce::String& paramId) const;
    int slotFor (int paramIndex) const;
    void bind (int slot, int paramIndex);
    void apply (int paramIndex, int slot, int value, bool pressed);

    juce::AudioProcessorValueTreeState& state;
    juce::Array<juce::RangedAudioParameter*> params;
    std::array<std::atomic<int>, 256> slotToParam;
    std::array<bool, 256> wasPressed {};
    std::atomic<int> armed { -1 };
};
