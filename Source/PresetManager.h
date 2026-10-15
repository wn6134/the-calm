#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Factory presets (built in) and user presets (XML files in the user's Application Support folder).
// A preset stores every parameter except the footswitches, their latch modes and mono/stereo.
class PresetManager
{
public:
    struct Factory
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values; // plain values; anything missing uses its default
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static const std::vector<Factory>& factoryPresets();
    static juce::File userFolder();
    static constexpr const char* extension = ".calmpreset";

    juce::StringArray userPresetNames() const;
    // Factory names first, then user names: the order the arrows step through.
    juce::StringArray allNames() const;

    bool loadFactory (int index);
    bool loadUser (const juce::String& name);
    bool load (const juce::String& name); // factory or user, by name
    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);
    void step (int delta);

    juce::String currentName() const;
    bool isUserPreset (const juce::String& name) const;
    // True when any stored parameter differs from what the current preset set.
    bool isModified() const;

private:
    using Values = std::map<juce::String, float>; // parameter id -> plain value

    std::optional<Values> presetValues (const juce::String& name) const;
    void apply (const Values&);
    void remember (const juce::String& name);
    bool storesParameter (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& state;
    // Normalised values the current preset sets, rebuilt whenever the preset name changes.
    mutable std::map<juce::String, float> snapshot;
    mutable juce::String snapshotName;
};
